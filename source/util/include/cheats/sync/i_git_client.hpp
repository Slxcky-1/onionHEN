#pragma once

#include "cheats/sync/types.hpp"

#include <cstddef>

namespace onion::cheats::sync {

struct GitProgress {
  const char *phase = "";
  unsigned received = 0;
  unsigned total = 0;
};

using GitProgressFn = void (*)(const GitProgress &progress, void *user);

struct GitCloneOpts {
  const char *branch = "master";
  int depth = 1;
  /** Working-tree pathspecs from the catalog (e.g. "cheats"). Not host names. */
  const char *const *checkout_paths = nullptr;
  size_t checkout_path_count = 0;
};

/**
 * Port for clone / fetch / inspect. No cheat types, no catalog knowledge.
 */
class IGitClient {
public:
  virtual ~IGitClient() = default;

  virtual GitStatus clone(const char *url, const char *dest,
                          const GitCloneOpts &opts) = 0;
  virtual GitStatus fetch(const char *repo_dir, const GitCloneOpts &opts) = 0;
  virtual GitStatus headSha(const char *repo_dir, char *out,
                            size_t out_size) = 0;
  virtual GitStatus remoteUrl(const char *repo_dir, char *out,
                              size_t out_size) = 0;

  virtual void setProgressHandler(GitProgressFn fn, void *user) {
    (void)fn;
    (void)user;
  }
};

} // namespace onion::cheats::sync
