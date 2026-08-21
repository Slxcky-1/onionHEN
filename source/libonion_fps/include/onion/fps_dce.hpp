/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Tier 1A: /dev/dce scanout counter (V-sync capped).
 * Follows PHU Games Tools by ArkSama (https://github.com/ArkSama).
 */
#pragma once

#include <cstdint>

namespace onion {
namespace fps {

class DceSource {
public:
  DceSource() = default;
  DceSource(const DceSource &) = delete;
  DceSource &operator=(const DceSource &) = delete;
  ~DceSource();

  /** Open /dev/dce. False if the node is missing or MAC-denied. */
  bool open();
  void close();
  bool is_open() const { return fd_ >= 0; }
  /** Session-level fail: do not retry open every tick. */
  bool unavailable() const { return unavailable_; }

  /** Read the current flip count. False on ioctl error. */
  bool sample(uint64_t *count);

private:
  int fd_ = -1;
  bool unavailable_ = false;
  bool logged_fail_ = false;
};

} // namespace fps
} // namespace onion
