/* Copyright (C) 2026 OnionHEN / LightningMods */
#include <onion/fps_formula.hpp>
#include <onion/fps_sample.h>

#include <cstring>

namespace onion {
namespace fps {

double hz_from_delta(uint64_t dcount, double dt_sec) {
  if (dcount == 0 || dt_sec <= 0.0)
    return 0.0;
  const double hz = static_cast<double>(dcount) / dt_sec;
  if (hz < static_cast<double>(kFpsMin) || hz > static_cast<double>(kFpsMax))
    return 0.0;
  return hz;
}

float rolling_mean(const float *samples, int n) {
  if (!samples || n <= 0)
    return 0.f;
  double sum = 0.0;
  for (int i = 0; i < n; ++i)
    sum += static_cast<double>(samples[i]);
  return static_cast<float>(sum / static_cast<double>(n));
}

HybridOut compose(const HybridIn &in) {
  HybridOut out;
  out.dead_ticks = in.dead_ticks;

  const bool scanout = in.scanout_ok && in.scanout >= kFpsMin;
  const bool ring = in.ring_ok && in.ring >= kFpsMin;
  const bool global = in.global_ok && in.global >= kFpsMin;
  if (!scanout && !ring && !global) {
    out.dead_ticks = in.dead_ticks + 1;
    return out;
  }
  out.dead_ticks = 0;

  const float calibrated = ring ? in.ring : (global ? in.global : 0.f);
  float denom = 0.f;
  if (ring)
    denom = in.ring;
  else if (scanout)
    denom = in.scanout;
  const float cal_ratio =
      (global && denom >= kFpsMin) ? (in.global / denom) : 1.f;

  float main = 0.f;
  uint8_t src = 0;
  if (scanout)
    src |= ONION_FPS_SRC_SCANOUT;
  if (ring)
    src |= ONION_FPS_SRC_RING;
  if (global)
    src |= ONION_FPS_SRC_GLOBAL;

  if (cal_ratio > kMultiPassRatio && global && in.global < kFpsMax && scanout) {
    main = in.scanout;
    src |= ONION_FPS_SRC_MULTIPASS;
  } else {
    main = in.scanout;
    if (calibrated > main)
      main = calibrated;
    if (scanout && calibrated > 0.f)
      src |= ONION_FPS_SRC_HYBRID;
  }

  if (main < kFpsMin) {
    out.dead_ticks = in.dead_ticks + 1;
    return out;
  }
  if (out.dead_ticks >= kDeadTicks)
    return out;

  out.valid = true;
  out.fps = main;
  out.source = src;
  return out;
}

bool is_ps5_native_title(const char *title_id) {
  if (!title_id || std::strlen(title_id) < 4)
    return false;
  return std::strncmp(title_id, "PPSA", 4) == 0 ||
         std::strncmp(title_id, "PPSB", 4) == 0;
}

bool is_ps4_bc_title(const char *title_id) {
  if (!title_id || std::strlen(title_id) < 4)
    return false;
  static const char *const kPs4[] = {"CUSA", "PCAS", "PCJS", "PCKS", "CUHJ"};
  for (const char *p : kPs4) {
    if (std::strncmp(title_id, p, 4) == 0)
      return true;
  }
  return false;
}

} // namespace fps
} // namespace onion
