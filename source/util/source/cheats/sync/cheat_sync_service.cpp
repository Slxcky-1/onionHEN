#include "cheats/sync/cheat_sync_service.hpp"

#include "cheats/cheat_repository.hpp"
#include "cheats/runtime.h"
#include "cheats/sync/cheat_catalog_registry.hpp"
#include "cheats/sync/git_mirror_factory.hpp"
#include "cheats/sync/http_transport_ps5.hpp"
#include "cheats/sync/libgit2_client.hpp"

#include <onion/fs.h>
#include <onion/log.h>
#include <onion/notify.h>
#include <onion/notify_i18n.h>

#include <pthread.h>
#include <sys/statvfs.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

extern "C" int sceSystemServiceParamGetInt(int param_id, int *value);

namespace onion::cheats::sync {
namespace {

int flatten_existing(const char *root) {
  CheatRepository::ensureCheatsDir();
  return CheatRepository::flattenInstallTree(root ? root : "");
}

int read_system_language(const onion::Settings &settings) {
  int system_language = 1;
  if (settings.ui_lang == onion::kUiLanguageSystem) {
    (void)sceSystemServiceParamGetInt(1, &system_language);
  }
  return system_language;
}

bool enough_free_space(const char *path, unsigned long long min_bytes) {
  struct statvfs st {};
  if (!path || statvfs(path, &st) != 0) {
    return true;
  }
  const unsigned long long avail =
      static_cast<unsigned long long>(st.f_bavail) *
      static_cast<unsigned long long>(st.f_frsize);
  return avail >= min_bytes;
}

struct WorkerArg {
  CheatSyncService *svc;
  onion::Settings settings;
  std::string catalog_id;
  std::string mirror_override;
};

void *worker_thunk(void *raw) {
  std::unique_ptr<WorkerArg> arg(static_cast<WorkerArg *>(raw));
  arg->svc->worker(arg->settings, std::move(arg->catalog_id),
                   std::move(arg->mirror_override));
  return nullptr;
}

} // namespace

CheatSyncService &CheatSyncService::instance() {
  static CheatSyncService svc;
  return svc;
}

CheatSyncService::CheatSyncService() = default;
CheatSyncService::~CheatSyncService() = default;

void CheatSyncService::setGitClientForTest(IGitClient *client) {
  std::lock_guard<std::mutex> lock(mu_);
  test_git_ = client;
}

IGitClient &CheatSyncService::gitClient() {
  if (test_git_) {
    return *test_git_;
  }
  static Ps5HttpTransport http;
  static LibGit2Client git(&http);
  return git;
}

CheatSyncStatus CheatSyncService::status() const {
  std::lock_guard<std::mutex> lock(mu_);
  return status_;
}

CheatSyncService::StartResult
CheatSyncService::start(const onion::Settings &settings, const char *catalog_id,
                        const char *mirror_override) {
  std::lock_guard<std::mutex> lock(mu_);
  if (running_) {
    return StartResult::AlreadyRunning;
  }
  if (!enough_free_space(ONION_DATA_ROOT, 256ull * 1024ull * 1024ull)) {
    status_.state = CheatSyncStatus::State::Error;
    status_.error = "no_space";
    return StartResult::Rejected;
  }

  running_ = true;
  status_.state = CheatSyncStatus::State::Running;
  status_.error.clear();
  status_.sha.clear();
  status_.catalog_id = catalog_id ? catalog_id : "";

  auto *arg = new WorkerArg{this, settings, catalog_id ? catalog_id : "",
                            mirror_override ? mirror_override : ""};
  pthread_t tid;
  if (pthread_create(&tid, nullptr, worker_thunk, arg) != 0) {
    delete arg;
    running_ = false;
    status_.state = CheatSyncStatus::State::Error;
    status_.error = "thread";
    return StartResult::Rejected;
  }
  pthread_detach(tid);
  return StartResult::Started;
}

void CheatSyncService::worker(onion::Settings settings, std::string catalog_id,
                              std::string mirror_override) {
  const ICheatCatalog *catalog =
      CheatCatalogRegistry::find(catalog_id.empty() ? nullptr : catalog_id.c_str());
  CheatSyncStatus done;
  if (!catalog) {
    done.state = CheatSyncStatus::State::Error;
    done.error = "unknown_catalog";
    onion_notify(true, "notify.cheats.sync.error", done.error.c_str());
    std::lock_guard<std::mutex> lock(mu_);
    status_ = done;
    running_ = false;
    return;
  }

  CheatMirrorPref pref =
      static_cast<CheatMirrorPref>(settings.cheats_mirror);
  if (!mirror_override.empty()) {
    pref = GitMirrorFactory::parsePref(mirror_override.c_str(), pref);
  }

  GitMirrorPick pick = GitMirrorFactory::create(
      pref, settings.ui_lang, read_system_language(settings));

  onion_notify(true, "notify.cheats.sync.start", pick.primary->name());
  LOG_INFO("cheat sync catalog=%s mirror=%s url=%s", catalog->id(),
           pick.primary->name(), pick.primary->cloneUrl(*catalog).c_str());

  CheatSyncEngine engine(gitClient(), flatten_existing, if_exists, rmtree);
  CheatSyncEngine::Result result =
      engine.run(*catalog, *pick.primary, pick.fallback.get(), ONION_DATA_ROOT);

  done.catalog_id = catalog->id();
  done.mirror = result.used_mirror;
  done.url = result.url;
  done.sha = result.sha;
  if (result.status == GitStatus::Ok) {
    done.state = CheatSyncStatus::State::Ok;
    onion_notify(true, "notify.cheats.sync.ok",
                 done.sha.empty() ? catalog->id() : done.sha.c_str());
    LOG_INFO("cheat sync ok catalog=%s sha=%s", catalog->id(),
             done.sha.c_str());
  } else {
    done.state = CheatSyncStatus::State::Error;
    done.error = result.error.empty() ? "sync_failed" : result.error;
    onion_notify(true, "notify.cheats.sync.error", done.error.c_str());
    LOG_ERROR("cheat sync failed catalog=%s err=%s", catalog->id(),
              done.error.c_str());
  }

  std::lock_guard<std::mutex> lock(mu_);
  status_ = done;
  running_ = false;
}

} // namespace onion::cheats::sync
