/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Skip-hook FPS sampler. Does not inject the game and does not load an SPRX.
 * Reads /dev/dce + DMAP of the game's already-loaded libSceAgcDriver.sprx.
 * Sampling approach follows PHU Games Tools by ArkSama
 * (https://github.com/ArkSama).
 */
#include "daemon_ops.hpp"
#include "globalconf.hpp"

#include <onion/fps_agc.hpp>
#include <onion/fps_dce.hpp>
#include <onion/fps_formula.hpp>
#include <onion/fps_publish.hpp>
#include <onion/log.h>
#include <onion/proc_query.h>
#include <onion/settings.hpp>

#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>

namespace {

constexpr useconds_t kSampleUs = 33333;
constexpr unsigned kIdleSleepSec = 1;
constexpr unsigned kPidRefreshSec = 1;

struct CounterState {
  uint64_t count = 0;
  double t = 0.0;
  bool have = false;
};

double monotonic_sec() {
  struct timespec ts {};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return 0.0;
  return static_cast<double>(ts.tv_sec) +
         static_cast<double>(ts.tv_nsec) * 1e-9;
}

bool sample_hz(CounterState &st, uint64_t count, float *out) {
  const double now = monotonic_sec();
  if (!st.have) {
    st.count = count;
    st.t = now;
    st.have = true;
    return false;
  }
  if (count <= st.count || now <= st.t) {
    st.count = count;
    st.t = now;
    return false;
  }
  const double hz =
      onion::fps::hz_from_delta(count - st.count, now - st.t);
  st.count = count;
  st.t = now;
  if (hz <= 0.0)
    return false;
  *out = static_cast<float>(hz);
  return true;
}

void publish_invalid(int pid, const char *tid) {
  OnionFpsSample s {};
  s.pid = pid;
  s.valid = 0;
  s.unix_ns = onion_fps_realtime_ns();
  if (tid)
    std::strncpy(s.title_id, tid, sizeof(s.title_id) - 1);
  onion::fps::publish(s);
}

} // namespace

void *fps_sampler_thread(void *args) noexcept {
  (void)args;
  LOG_INFO("fps sampler started (skip-hook, %u us)",
           static_cast<unsigned>(kSampleUs));

  onion::fps::DceSource dce;
  onion::fps::AgcSources agc;
  CounterState scanout_st;
  CounterState ring_st;
  CounterState global_st;
  float window[onion::fps::kWindow] {};
  int window_n = 0;
  int window_i = 0;
  int dead_ticks = 0;
  pid_t cached_pid = -1;
  std::string cached_tid;
  time_t last_pid_check = 0;
  time_t last_cfg_check = 0;

  (void)onion::fps::publish_open();

  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    const time_t now_wall = time(nullptr);
    if (now_wall - last_cfg_check >= 1) {
      (void)LoadSettings(false);
      last_cfg_check = now_wall;
    }
    const onion::Settings cfg = g_settings.snapshot();
    if (!cfg.overlay_enabled || !cfg.overlay_fps) {
      publish_invalid(-1, nullptr);
      sleep(kIdleSleepSec);
      continue;
    }

    std::string tid;
    int app_id = 0;
    if (!Get_Running_App_TID(tid, app_id)) {
      cached_pid = -1;
      cached_tid.clear();
      scanout_st = {};
      ring_st = {};
      global_st = {};
      agc.reset();
      window_n = 0;
      window_i = 0;
      dead_ticks = 0;
      publish_invalid(-1, nullptr);
      sleep(kIdleSleepSec);
      continue;
    }

    const time_t now = time(nullptr);
    if (cached_pid <= 0 || !onion_proc_is_alive(cached_pid) ||
        tid != cached_tid || now - last_pid_check >= kPidRefreshSec) {
      const pid_t pid = onion_find_pid_ex("", false, true, false);
      last_pid_check = now;
      if (pid != cached_pid || tid != cached_tid) {
        scanout_st = {};
        ring_st = {};
        global_st = {};
        agc.reset();
        window_n = 0;
        window_i = 0;
        dead_ticks = 0;
      }
      cached_pid = pid;
      cached_tid = tid;
    }

    if (cached_pid <= 0 || onion::fps::is_ps4_bc_title(tid.c_str())) {
      publish_invalid(cached_pid > 0 ? static_cast<int>(cached_pid) : -1,
                      tid.c_str());
      usleep(kSampleUs);
      continue;
    }

    onion::fps::HybridIn hin;
    hin.dead_ticks = dead_ticks;
    uint64_t c = 0;
    float hz = 0.f;
    if (!dce.unavailable() && dce.sample(&c) && sample_hz(scanout_st, c, &hz)) {
      hin.scanout_ok = true;
      hin.scanout = hz;
    }
    if (onion::fps::is_ps5_native_title(tid.c_str())) {
      if (agc.sample_ring(cached_pid, &c) && sample_hz(ring_st, c, &hz)) {
        hin.ring_ok = true;
        hin.ring = hz;
      }
      if (agc.sample_global(cached_pid, &c) && sample_hz(global_st, c, &hz)) {
        hin.global_ok = true;
        hin.global = hz;
      }
    }

    onion::fps::HybridOut hout = onion::fps::compose(hin);
    dead_ticks = hout.dead_ticks;
    if (hout.valid) {
      window[window_i] = hout.fps;
      window_i = (window_i + 1) % onion::fps::kWindow;
      if (window_n < onion::fps::kWindow)
        ++window_n;
      hout.fps = onion::fps::rolling_mean(window, window_n);
    }

    OnionFpsSample sample {};
    sample.pid = static_cast<int>(cached_pid);
    sample.valid = hout.valid ? 1 : 0;
    sample.source = hout.source;
    sample.fps = hout.valid ? hout.fps : 0.f;
    sample.unix_ns = onion_fps_realtime_ns();
    std::strncpy(sample.title_id, tid.c_str(), sizeof(sample.title_id) - 1);
    onion::fps::publish(sample);

    usleep(kSampleUs);
  }

  onion::fps::publish_close();
  return nullptr;
}
