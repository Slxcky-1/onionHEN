/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Remote Play pairing state shared by the session and ShellUI presentation.
 */
#pragma once

#include <cstdint>

namespace remote_play {

constexpr uint32_t kPairingTimeoutSeconds = 120;
constexpr uint64_t kPairingTimeoutMilliseconds =
    static_cast<uint64_t>(kPairingTimeoutSeconds) * 1000u;
constexpr uint32_t kPairingNotificationIntervalSeconds = 10;

/** Round up so a newly generated PIN starts at the full 120 seconds. */
inline uint32_t seconds_remaining(uint64_t deadline_ms, uint64_t now_ms) {
  if (deadline_ms <= now_ms)
    return 0;

  uint64_t remaining_ms = deadline_ms - now_ms;
  if (remaining_ms > kPairingTimeoutMilliseconds)
    remaining_ms = kPairingTimeoutMilliseconds;
  return static_cast<uint32_t>((remaining_ms - 1u) / 1000u + 1u);
}

/** Return the 10-second notification mark for a live countdown. */
inline uint32_t countdown_notification_mark(uint32_t remaining_seconds) {
  if (remaining_seconds == 0)
    return 0;
  if (remaining_seconds > kPairingTimeoutSeconds)
    remaining_seconds = kPairingTimeoutSeconds;
  return ((remaining_seconds - 1u) / kPairingNotificationIntervalSeconds + 1u) *
         kPairingNotificationIntervalSeconds;
}

} // namespace remote_play
