#pragma once

#include "cheats/sync/i_cheat_catalog.hpp"
#include "cheats/sync/i_cheat_mirror.hpp"
#include "cheats/sync/i_http_transport.hpp"
#include "cheats/sync/types.hpp"

#include <string>

namespace onion::cheats::sync {

/** Download one ZIP, extract only catalog roots, install, then clean up. */
class CheatSyncEngine {
public:
  using FlattenFn = int (*)(const char *root);

  struct Result {
    SyncStatus status = SyncStatus::Rejected;
    CheatMirrorId used_mirror = CheatMirrorId::Github;
    std::string url;
    std::string error;
  };

  CheatSyncEngine(IHttpTransport &http, FlattenFn flatten);

  void setProgressHandler(SyncProgressFn fn, void *user);

  Result run(const ICheatCatalog &catalog, const ICheatMirror &primary,
             const ICheatMirror *fallback, const char *data_root);

private:
  SyncStatus tryOne(const ICheatCatalog &catalog, const ICheatMirror &mirror,
                    const char *data_root, Result &out);

  IHttpTransport &http_;
  FlattenFn flatten_ = nullptr;
  SyncProgressFn progress_ = nullptr;
  void *progress_user_ = nullptr;
};

} // namespace onion::cheats::sync
