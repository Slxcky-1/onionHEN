/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Tier 1E DCB ring + Tier 1F AgcDriver global submit counter.
 * Follows PHU Games Tools by ArkSama (https://github.com/ArkSama).
 */
#pragma once

#include <cstdint>
#include <sys/types.h>

namespace onion {
namespace fps {

class AgcSources {
public:
  void reset();

  bool sample_ring(pid_t pid, uint64_t *count);
  bool sample_global(pid_t pid, uint64_t *count);

private:
  pid_t pid_ = -1;
  uint64_t global_va_ = 0;
  bool logged_ring_ = false;
  bool logged_global_ = false;
};

} // namespace fps
} // namespace onion
