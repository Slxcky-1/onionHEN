#pragma once

#include "cheats/sync/types.hpp"

#include <cstddef>

namespace onion::cheats::sync {

struct GitCloneOpts {
  const char *branch = "master";
  int depth = 1;
};

/**
 * Port for clone / fetch / inspect. No cheat types, no catalog knowledge.
 */
class IGitClient {
public:
  virtual ~IGitClient() = default;

  virtual GitStatus clone(const char *url, const char *dest,
                          const GitCloneOpts &opts) = 0;
  virtual GitStatus fetch(const char *repo_dir) = 0;
  virtual GitStatus headSha(const char *repo_dir, char *out,
                            size_t out_size) = 0;
  virtual GitStatus remoteUrl(const char *repo_dir, char *out,
                              size_t out_size) = 0;
};

} // namespace onion::cheats::sync
