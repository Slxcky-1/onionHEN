#include <onion/fs.h>
#include <onion/log.h>
#include "cheats/cheat_repository.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <strings.h>
#include <string_view>
#include <sys/stat.h>
#include <unordered_map>
#include <unistd.h>

#include "cheats/i_cheat_parser.hpp"
#include "cheats/cheat_engine.h"
#include "cheats/runtime.h"

namespace onion::cheats {

namespace {

struct ResolveCacheEntry {
  FileSignature directory;
  std::string path;
};

std::mutex g_resolve_cache_mutex;
std::unordered_map<std::string, ResolveCacheEntry> g_resolve_cache;

void uppercaseAscii(char *value) {
  if (value == nullptr) {
    return;
  }
  for (size_t i = 0; value[i] != '\0'; ++i) {
    value[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[i])));
  }
}

void lowercaseAscii(char *value) {
  if (value == nullptr) {
    return;
  }
  for (size_t i = 0; value[i] != '\0'; ++i) {
    value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
  }
}

int64_t statMtimeNsec(const struct stat &st) {
#if defined(__APPLE__)
  return static_cast<int64_t>(st.st_mtimespec.tv_nsec);
#else
  return static_cast<int64_t>(st.st_mtim.tv_nsec);
#endif
}

int64_t statCtimeNsec(const struct stat &st) {
#if defined(__APPLE__)
  return static_cast<int64_t>(st.st_ctimespec.tv_nsec);
#else
  return static_cast<int64_t>(st.st_ctim.tv_nsec);
#endif
}

void fillSignature(FileSignature &out, const std::string &path,
                   const struct stat &st) {
  out.path = path;
  out.size = static_cast<uint64_t>(st.st_size);
  out.inode = static_cast<uint64_t>(st.st_ino);
  out.mtime = static_cast<int64_t>(st.st_mtime);
  out.mtime_nsec = statMtimeNsec(st);
  out.ctime = static_cast<int64_t>(st.st_ctime);
  out.ctime_nsec = statCtimeNsec(st);
}

bool readDirectorySignature(FileSignature &out) {
  struct stat st {};
  if (::stat(ONION_CHEATS_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
    return false;
  }
  fillSignature(out, ONION_CHEATS_DIR, st);
  return true;
}

std::string findExactPath(const std::string &basename) {
  for (int rank = 0;; ++rank) {
    const char *extension = onion_cheat_extension_for_rank(rank);
    if (extension == nullptr) {
      return {};
    }
    const std::string path = std::string(ONION_CHEATS_DIR) + "/" + basename +
                             "." + extension;
    if (CheatRepository::fileExists(path)) {
      return path;
    }
  }
}

bool isRegularEntry(const char *directory, const struct dirent *entry) {
  if (directory == nullptr || entry == nullptr) {
    return false;
  }
#ifdef DT_DIR
  if (entry->d_type == DT_DIR) {
    return false;
  }
#endif
#ifdef DT_REG
  if (entry->d_type == DT_REG) {
    return true;
  }
#endif

  char path[512];
  const int written =
      std::snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(path)) {
    return false;
  }
  struct stat st {};
  return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

struct ScannedCandidate {
  onion_cheat_filename_t parts{};
  std::string name;
};

std::string scanMatchingPath(const char *title_id, const std::string &version,
                             std::string_view process,
                             std::string_view hash) {
  DIR *directory = ::opendir(ONION_CHEATS_DIR);
  if (directory == nullptr) {
    return {};
  }

  ScannedCandidate best;
  size_t match_count = 0;
  while (const struct dirent *entry = ::readdir(directory)) {
    onion_cheat_filename_t parts{};
    if (onion_cheat_parse_filename(entry->d_name, &parts) < 0 ||
        strcasecmp(parts.title_id, title_id) != 0 ||
        std::strcmp(parts.version, version.c_str()) != 0 ||
        !onion_cheat_filename_compatible(&parts, process.data(), hash.data()) ||
        !isRegularEntry(ONION_CHEATS_DIR, entry)) {
      continue;
    }

    ++match_count;
    if (best.name.empty() ||
        onion_cheat_filename_compare(&parts, entry->d_name, &best.parts,
                                     best.name.c_str(), process.data(),
                                     hash.data()) < 0) {
      best.parts = parts;
      best.name = entry->d_name;
    }
  }
  ::closedir(directory);

  if (best.name.empty()) {
    return {};
  }

  const std::string path = std::string(ONION_CHEATS_DIR) + "/" + best.name;
  if (match_count > 1 && best.parts.hash[0] != '\0' && hash.empty()) {
    LOG_WARN("[repository] multiple hashed cheats for %s %s; using %s",
             title_id, version.c_str(), best.name.c_str());
  } else if (best.parts.hash[0] != '\0' || best.parts.process[0] != '\0') {
    LOG_INFO("[repository] using cheat %s", path.c_str());
  }
  return path;
}

std::string resolveScannedPath(const char *title_id, const std::string &version,
                               std::string_view process,
                               std::string_view hash) {
  FileSignature directory;
  if (!readDirectorySignature(directory)) {
    return {};
  }

  const std::string key = std::string(title_id) + "\n" + version + "\n" +
                          std::string(process) + "\n" + std::string(hash);
  bool cache_hit = false;
  std::string cached_path;
  {
    std::lock_guard<std::mutex> lock(g_resolve_cache_mutex);
    const auto it = g_resolve_cache.find(key);
    if (it != g_resolve_cache.end() &&
        it->second.directory.sameIdentity(directory)) {
      cache_hit = true;
      cached_path = it->second.path;
    }
  }
  if (cache_hit &&
      (cached_path.empty() || CheatRepository::fileExists(cached_path))) {
    return cached_path;
  }

  const std::string path = scanMatchingPath(title_id, version, process, hash);
  {
    std::lock_guard<std::mutex> lock(g_resolve_cache_mutex);
    g_resolve_cache[key] = {directory, path};
  }
  return path;
}

} // namespace

std::string CheatRepository::resolvePath(const game_context_t &game) {
  char title_id[sizeof(game.title_id)];
  char version[32];
  char process[sizeof(game.process_name)];
  char hash[sizeof(game.process_hash)];

  if (game.title_id[0] == '\0' || game.version[0] == '\0' ||
      std::strcmp(game.version, "unknown") == 0) {
    return {};
  }
  std::snprintf(title_id, sizeof(title_id), "%s", game.title_id);
  uppercaseAscii(title_id);
  onion_cheat_normalize_filename_token(game.version, version, sizeof(version));
  onion_cheat_normalize_filename_token(game.process_name, process,
                                       sizeof(process));
  onion_cheat_normalize_filename_token(game.process_hash, hash, sizeof(hash));
  lowercaseAscii(hash);

  const std::string basename = std::string(title_id) + "_" + version;
  if (process[0] != '\0') {
    if (const std::string path = findExactPath(basename + "_" + process);
        !path.empty()) {
      return path;
    }
  }
  if (const std::string path = findExactPath(basename); !path.empty()) {
    return path;
  }
  if (hash[0] != '\0') {
    if (process[0] != '\0' && !onion_cheat_is_eboot_process(process)) {
      if (const std::string path =
              findExactPath(basename + "_" + process + "_" + hash);
          !path.empty()) {
        return path;
      }
    } else if (const std::string path = findExactPath(basename + "_" + hash);
               !path.empty()) {
      return path;
    }
  }

  return resolveScannedPath(title_id, version, process, hash);
}

bool CheatRepository::fileExists(const std::string &path) {
  return !path.empty() && if_exists(path.c_str());
}

bool CheatRepository::statSignature(const std::string &path,
                                    FileSignature &out) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    return false;
  }
  fillSignature(out, path, st);
  return true;
}

int CheatRepository::loadFile(const std::string &path, onion_cheat_file_t &out) {
  return CheatParserFactory::loadFile(path, out);
}

void CheatRepository::ensureCheatsDir() {
  ::mkdir(ONION_DATA_ROOT, 0777);
  ::mkdir(ONION_CHEATS_DIR, 0777);
}

int CheatRepository::flattenInstallTree(const std::string &root) {
  return onion_cheat_flatten_install_tree(root.c_str());
}

int CheatRepository::flattenInstallTree(const std::string &root,
                                        onion_cheat_progress_fn progress,
                                        void *user) {
  return onion_cheat_flatten_install_tree_with_progress(root.c_str(), progress,
                                                        user);
}

} // namespace onion::cheats
