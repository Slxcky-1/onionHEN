#pragma once

#include <cstddef>

namespace onion::cheats::sync {

/** User-facing [cheats] mirror= token. */
enum class CheatMirrorPref : int {
  Auto = 0,
  Github = 1,
  Cnb = 2,
};

/** Concrete git host. Catalog::slugFor() is keyed by this. */
enum class CheatMirrorId : int {
  Github = 1,
  Cnb = 2,
};

/** Narrow result codes shared by git + sync. 0 is success. */
enum class GitStatus : int {
  Ok = 0,
  Network = -1,
  Io = -2,
  Protocol = -3,
  Unavailable = -4,
  NoSpace = -5,
  Busy = -6,
  Rejected = -7,
};

inline bool is_network_failure(GitStatus s) {
  return s == GitStatus::Network || s == GitStatus::Protocol;
}

} // namespace onion::cheats::sync
