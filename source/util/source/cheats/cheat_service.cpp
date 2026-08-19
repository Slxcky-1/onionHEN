#include <onion/log.h>
#include "cheats/cheat_service.hpp"

#include "onion_cjson.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

extern "C" {
int sceKernelGetProcessName(int pid, char *name);
}

namespace onion::cheats {

CheatService &CheatService::instance() {
  static CheatService svc;
  return svc;
}

CheatService::CheatService() {
  std::memset(&game_, 0, sizeof(game_));
  std::memset(&file_, 0, sizeof(file_));
  file_.master_code_id = -1;
}

CheatService::~CheatService() {
  std::lock_guard<std::mutex> lock(mu_);
  onion_cheat_file_clear(&file_);
}

void CheatService::ensureDir() { CheatRepository::ensureCheatsDir(); }

void CheatService::onGameExec(pid_t pid, const char *title_id, int appid) {
  std::lock_guard<std::mutex> lock(mu_);
  disableEnabledLocked("game exec");
  clearFileLocked();
  has_tracked_game_ = true;
  tracked_pid_ = pid;
  std::memset(&game_, 0, sizeof(game_));
  game_.pid = pid;
  game_.appid = appid;
  if (title_id) {
    std::snprintf(game_.title_id, sizeof(game_.title_id), "%s", title_id);
  }
  LOG_INFO("[service] cheat track exec title=%s pid=%d",
               title_id ? title_id : "?", (int)pid);
}

void CheatService::onGameExit(pid_t pid) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!has_tracked_game_ || tracked_pid_ != pid) {
    return;
  }
  LOG_INFO("[service] cheat track exit title=%s pid=%d", game_.title_id,
               (int)pid);
  has_tracked_game_ = false;
  tracked_pid_ = 0;
  disableEnabledLocked("game exit");
  clearFileLocked();
  std::memset(&game_, 0, sizeof(game_));
}

int CheatService::fillGame(game_context_t &game, const std::string &title_id,
                           const std::string &version, int pid, int appid) {
  if (title_id.empty()) {
    return -1;
  }
  std::memset(&game, 0, sizeof(game));
  std::snprintf(game.title_id, sizeof(game.title_id), "%s", title_id.c_str());
  game.pid = pid;
  game.appid = appid;
  util_game_platform_from_title_id(title_id.c_str(), game.platform,
                                   sizeof(game.platform));

  if (!version.empty() && version != "unknown") {
    std::snprintf(game.version, sizeof(game.version), "%s", version.c_str());
  } else if (util_resolve_game_version(title_id.c_str(), game.version,
                                       sizeof(game.version)) < 0) {
    std::snprintf(game.version, sizeof(game.version), "unknown");
  }

  if (pid > 0) {
    game_context_t live{};
    if (util_get_running_bigapp(&live) == 0 &&
        title_id == live.title_id) {
      std::snprintf(game.process_name, sizeof(game.process_name), "%s",
                    live.process_name);
      if (game.appid == 0) {
        game.appid = live.appid;
      }
    } else {
      sceKernelGetProcessName(pid, game.process_name);
    }
  }
  return 0;
}

void CheatService::disableEnabledLocked(const char *reason) {
  if (!loaded_) {
    return;
  }
  for (size_t i = 0; i < file_.cheat_count; ++i) {
    if (!file_.cheats[i].enabled) {
      continue;
    }
    std::string status;
    if (applier_.toggle(game_, file_, static_cast<int>(i), status) < 0) {
      LOG_WARN("[service] disable stale cheat %zu (%s): %s", i,
                   reason ? reason : "?", status.c_str());
    }
  }
}

void CheatService::clearFileLocked() {
  onion_cheat_file_clear(&file_);
  loaded_ = false;
  sig_ = {};
}

int CheatService::refreshLocked(const game_context_t &game) {
  if (loaded_ && !sig_.path.empty() &&
      std::strcmp(game_.title_id, game.title_id) == 0 &&
      (game.version[0] == '\0' || std::strcmp(game.version, "unknown") == 0)) {
    game_.pid = game.pid;
    game_.appid = game.appid;
    return 0;
  }

  game_ = game;
  const std::string path = CheatRepository::resolvePath(game);
  if (path.empty() || !CheatRepository::fileExists(path)) {
    disableEnabledLocked("path unresolved");
    clearFileLocked();
    return -1;
  }

  FileSignature sig;
  if (!CheatRepository::statSignature(path, sig)) {
    disableEnabledLocked("stat failed");
    clearFileLocked();
    return -1;
  }

  if (!loaded_ || sig != sig_) {
    disableEnabledLocked("reload");
    onion_cheat_file_clear(&file_);
    if (CheatRepository::loadFile(path, file_) < 0) {
      clearFileLocked();
      return -1;
    }
    sig_ = std::move(sig);
    loaded_ = true;
    LOG_INFO("[service] loaded %s cheats=%zu", path.c_str(),
                 file_.cheat_count);
  }
  return 0;
}

int CheatService::writeListJson(const std::string &out_path) const {
  cJSON *root = cJSON_CreateObject();
  cJSON *authors = nullptr;
  cJSON *cheats = nullptr;
  if (!root || !cJSON_AddStringToObject(root, "name", file_.name) ||
      !(authors = cJSON_AddArrayToObject(root, "authors")) ||
      !(cheats = cJSON_AddArrayToObject(root, "cheats"))) {
    cJSON_Delete(root);
    return -1;
  }

  for (size_t a = 0; a < file_.author_count; ++a) {
    cJSON *author = cJSON_CreateString(file_.authors[a]);
    if (!author || !cJSON_AddItemToArray(authors, author)) {
      cJSON_Delete(author);
      cJSON_Delete(root);
      return -1;
    }
  }

  for (size_t i = 0; i < file_.cheat_count; ++i) {
    cJSON *cheat = cJSON_CreateObject();
    if (!cheat || !cJSON_AddItemToArray(cheats, cheat)) {
      cJSON_Delete(cheat);
      cJSON_Delete(root);
      return -1;
    }
    if (!cJSON_AddStringToObject(cheat, "name", file_.cheats[i].name) ||
        !cJSON_AddNumberToObject(cheat, "id", i) ||
        !cJSON_AddBoolToObject(cheat, "enabled", file_.cheats[i].enabled) ||
        !cJSON_AddStringToObject(cheat, "description",
                                 file_.cheats[i].description)) {
      cJSON_Delete(root);
      return -1;
    }
  }

  const std::string body = onion_cjson::print_owned(root);
  if (body.empty()) {
    return -1;
  }
  std::ofstream ofs(out_path, std::ios::trunc);
  if (!ofs) {
    return -1;
  }
  ofs.write(body.data(), static_cast<std::streamsize>(body.size()));
  return ofs.good() ? 0 : -1;
}

int CheatService::exportList(const std::string &title_id,
                             const std::string &version, int pid, int appid,
                             const std::string &out_path) {
  game_context_t game{};
  if (fillGame(game, title_id, version, pid, appid) < 0) {
    return -1;
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (refreshLocked(game) < 0) {
    return -1;
  }
  return writeListJson(out_path);
}

int CheatService::toggle(int pid, int appid, const std::string &title_id,
                         const std::string &version, int index,
                         std::string &status) {
  game_context_t game{};
  status.clear();
  if (fillGame(game, title_id, version, pid, appid) < 0) {
    status = "invalid game context";
    return -1;
  }
  if (pid > 0) {
    game.pid = pid;
  }
  if (appid != 0) {
    game.appid = appid;
  }

  std::lock_guard<std::mutex> lock(mu_);
  if (refreshLocked(game) < 0) {
    status = "unable to load cheat file";
    return -1;
  }
  if (index < 0 || static_cast<size_t>(index) >= file_.cheat_count) {
    status = "invalid cheat index";
    return -1;
  }
  game_.pid = game.pid;
  game_.appid = game.appid;
  return applier_.toggle(game_, file_, index, status);
}

int CheatService::flattenInstallTree(const std::string &root) {
  ensureDir();
  return CheatRepository::flattenInstallTree(root);
}

} // namespace onion::cheats
