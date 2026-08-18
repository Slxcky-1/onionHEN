#include "cheats/sync/cheat_sync_service.hpp"

#include "cheats/cheat_repository.hpp"
#include "cheats/runtime.h"
#include "cheats/sync/cheat_catalog_registry.hpp"
#include "cheats/sync/cheat_mirror_factory.hpp"
#include "cheats/sync/http_transport_ps5.hpp"

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

int flatten_existing(const char *root,
                     CheatSyncEngine::InstallProgressFn progress,
                     void *progress_user) {
  CheatRepository::ensureCheatsDir();
  return CheatRepository::flattenInstallTree(root ? root : "", progress,
                                             progress_user);
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

void on_sync_progress(const char *phase, size_t completed, size_t total,
                      void *user) {
  auto *svc = static_cast<CheatSyncService *>(user);
  if (!svc) {
    return;
  }
  int percent = -1;
  if (total > 0) {
    percent = static_cast<int>(
        (static_cast<unsigned long long>(completed) * 100ull) / total);
    if (percent > 100) {
      percent = 100;
    }
  }
  svc->noteProgress(phase ? phase : "", percent, completed, total);
}

} // namespace

CheatSyncService &CheatSyncService::instance() {
  static CheatSyncService svc;
  return svc;
}

CheatSyncService::CheatSyncService() = default;
CheatSyncService::~CheatSyncService() = default;

void CheatSyncService::setHttpTransportForTest(IHttpTransport *http) {
  std::lock_guard<std::mutex> lock(mu_);
  test_http_ = http;
}

IHttpTransport &CheatSyncService::httpTransport() {
  if (test_http_) {
    return *test_http_;
  }
  static Ps5HttpTransport http;
  return http;
}

CheatSyncStatus CheatSyncService::status() const {
  std::lock_guard<std::mutex> lock(mu_);
  return status_;
}

void CheatSyncService::noteProgress(const char *phase, int percent,
                                    size_t completed, size_t total) {
  int notify_percent = -1;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!running_) {
      return;
    }
    if (phase && phase[0]) {
      status_.phase = phase;
    }
    status_.progress_percent = percent;
    status_.completed = completed;
    status_.total = total;

    const bool transfer_phase = phase && std::strcmp(phase, "download") == 0;
    if (transfer_phase && percent >= 25) {
      int checkpoint = (percent / 25) * 25;
      if (checkpoint > 75) {
        checkpoint = 75;
      }
      if (checkpoint > last_progress_notify_percent_) {
        last_progress_notify_percent_ = checkpoint;
        notify_percent = checkpoint;
      }
    }
  }
  if (notify_percent > 0) {
    onion_notify(true, "notify.cheats.sync.progress", notify_percent);
  }
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
  status_.phase = "start";
  status_.progress_percent = 0;
  status_.completed = 0;
  status_.total = 0;
  status_.catalog_id = catalog_id ? catalog_id : "";
  last_progress_notify_percent_ = -1;

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
    pref = CheatMirrorFactory::parsePref(mirror_override.c_str(), pref);
  }

  const int system_lang = read_system_language(settings);
  CheatMirrorPick pick =
      CheatMirrorFactory::create(pref, settings.ui_lang, system_lang);

  onion_notify(true, "notify.cheats.sync.start", pick.primary->name());
  LOG_INFO("cheat sync catalog=%s pref=%s primary=%s fallback=%s url=%s",
           catalog->id(), CheatMirrorFactory::prefName(pref),
           pick.primary->name(), pick.fallback ? pick.fallback->name() : "-",
           pick.primary->archiveUrl(*catalog).c_str());

  CheatSyncEngine engine(httpTransport(), flatten_existing);
  engine.setProgressHandler(on_sync_progress, this);
  CheatSyncEngine::Result result =
      engine.run(*catalog, *pick.primary, pick.fallback.get(), ONION_DATA_ROOT);
  engine.setProgressHandler(nullptr, nullptr);

  done.catalog_id = catalog->id();
  done.mirror = result.used_mirror;
  done.url = result.url;
  if (result.status == SyncStatus::Ok) {
    done.state = CheatSyncStatus::State::Ok;
    onion_notify(true, "notify.cheats.sync.ok", catalog->id());
    LOG_INFO("cheat sync ok catalog=%s mirror=%s", catalog->id(),
             pick.primary->name());
  } else {
    done.state = CheatSyncStatus::State::Error;
    done.error = result.error.empty() ? "sync_failed" : result.error;
    if (result.status == SyncStatus::Clock) {
      onion_notify(true, "notify.cheats.sync.clock");
    } else {
      onion_notify(true, "notify.cheats.sync.error", done.error.c_str());
    }
    LOG_ERROR("cheat sync failed catalog=%s err=%s", catalog->id(),
              done.error.c_str());
  }

  std::lock_guard<std::mutex> lock(mu_);
  status_ = done;
  running_ = false;
}

} // namespace onion::cheats::sync
