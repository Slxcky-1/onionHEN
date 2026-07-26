#include <onion/log.h>
#include "cheats/cheat_repository.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include "cheats/i_cheat_parser.hpp"
#include "cheats/cheat_engine.h"
#include "cheats/runtime.h"

extern "C" {
}

namespace onion::cheats {

std::string CheatRepository::resolvePath(const game_context_t &game) {
  static const char *exts[] = {"json", "shn", "mc4", "ShnExt"};
  char version[32];
  char path[512];

  if (game.title_id[0] == '\0' || game.version[0] == '\0' ||
      std::strcmp(game.version, "unknown") == 0) {
    return {};
  }
  onion_cheat_normalize_version(game.version, version, sizeof(version));
  for (const char *ext : exts) {
    std::snprintf(path, sizeof(path), ONION_CHEATS_DIR "/%s_%s.%s",
                  game.title_id, version, ext);
    if (fileExists(path)) {
      return path;
    }
  }
  return {};
}

bool CheatRepository::fileExists(const std::string &path) {
  struct stat st {};
  return !path.empty() && ::stat(path.c_str(), &st) == 0;
}

bool CheatRepository::statSignature(const std::string &path,
                                    FileSignature &out) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    return false;
  }
  out.path = path;
  out.size = static_cast<uint64_t>(st.st_size);
  out.inode = static_cast<uint64_t>(st.st_ino);
  out.mtime = static_cast<int64_t>(st.st_mtime);
  out.ctime = static_cast<int64_t>(st.st_ctime);
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

} // namespace onion::cheats
