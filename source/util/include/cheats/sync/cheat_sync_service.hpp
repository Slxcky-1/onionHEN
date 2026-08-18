#pragma once

#include "cheats/sync/cheat_sync_engine.hpp"
#include "cheats/sync/types.hpp"

#include <mutex>
#include <string>

#ifdef __cplusplus
#include <onion/settings.hpp>
#endif

namespace onion::cheats::sync {

class IGitClient;
class IHttpTransport;

struct CheatSyncStatus {
  enum class State { Idle, Running, Ok, Error } state = State::Idle;
  CheatMirrorId mirror = CheatMirrorId::Github;
  std::string url;
  std::string sha;
  std::string catalog_id;
  std::string error;
  std::string phase;
  int progress_percent = -1;
};

/**
 * Process facade for IPC. Owns the worker thread and last status.
 * Does not implement flatten or URL construction itself.
 */
class CheatSyncService {
public:
  enum class StartResult { Started, AlreadyRunning, Rejected };

  static CheatSyncService &instance();

  StartResult start(const onion::Settings &settings, const char *catalog_id,
                    const char *mirror_override);

  CheatSyncStatus status() const;

  /** Test seam: replace the git client. Null restores the production client. */
  void setGitClientForTest(IGitClient *client);
  void setHttpTransportForTest(IHttpTransport *http);

  void worker(onion::Settings settings, std::string catalog_id,
              std::string mirror_override);

  void noteProgress(const char *phase, int percent);

  CheatSyncService(const CheatSyncService &) = delete;
  CheatSyncService &operator=(const CheatSyncService &) = delete;

private:
  CheatSyncService();
  ~CheatSyncService();

  IGitClient &gitClient();
  IHttpTransport &httpTransport();

  mutable std::mutex mu_;
  CheatSyncStatus status_{};
  bool running_ = false;
  IGitClient *test_git_ = nullptr;
  IHttpTransport *test_http_ = nullptr;
};

} // namespace onion::cheats::sync
