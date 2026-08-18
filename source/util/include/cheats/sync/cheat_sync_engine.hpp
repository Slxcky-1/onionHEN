#pragma once

#include "cheats/sync/i_cheat_catalog.hpp"
#include "cheats/sync/i_git_client.hpp"
#include "cheats/sync/i_git_mirror.hpp"
#include "cheats/sync/types.hpp"

#include <string>
#include <vector>

namespace onion::cheats::sync {

/**
 * Orchestrates catalog + mirror + git + flatten. No threads, no IPC, no
 * libgit2 types. Collaborators are injected so host tests can mock git.
 */
class CheatSyncEngine {
public:
  using FlattenFn = int (*)(const char *root);
  using ExistsFn = bool (*)(const char *path);
  using RmtreeFn = bool (*)(const char *path);

  struct Result {
    GitStatus status = GitStatus::Rejected;
    CheatMirrorId used_mirror = CheatMirrorId::Github;
    std::string url;
    std::string sha;
    std::string error;
    std::vector<std::string> flattened_roots;
    std::vector<std::string> discarded_paths;
  };

  CheatSyncEngine(IGitClient &git, FlattenFn flatten, ExistsFn exists,
                  RmtreeFn rmtree);

  Result run(const ICheatCatalog &catalog, const IGitMirror &primary,
             const IGitMirror *fallback, const char *data_root);

private:
  GitStatus tryOne(const ICheatCatalog &catalog, const IGitMirror &mirror,
                   const char *dest, Result &out);

  IGitClient &git_;
  FlattenFn flatten_;
  ExistsFn exists_;
  RmtreeFn rmtree_;
};

} // namespace onion::cheats::sync
