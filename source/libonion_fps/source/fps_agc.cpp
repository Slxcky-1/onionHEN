/* Copyright (C) 2026 OnionHEN / LightningMods */
#include <onion/fps_agc.hpp>

#include <onion/log.h>
#include <onion/proc_dmap.h>

#include <ps5/kernel.h>

#include <cstdint>

namespace onion {
namespace fps {
namespace {

/* PHU videoout_dcb_ring::init: mov rbx, 0xFE03B72EC */
constexpr uint64_t kRingVa = 0xFE03B72ECULL;
constexpr uint32_t kRingOffSize = 0;
constexpr uint32_t kRingOffIdx = 4;
constexpr uint32_t kRingOffEntries = 0xC;
constexpr uint32_t kSlotBytes = 32;
constexpr uint32_t kSlotCountOff = 8;
constexpr uint32_t kDefaultRing = 64;
constexpr uint32_t kMaxRing = 0x1000;
constexpr uint32_t kWalkAllLimit = 100;
constexpr uint64_t kGlobalOff = 0x1AAC0;

bool read_u32(pid_t pid, uint64_t va, uint32_t *out) {
  return onion_proc_copyout(pid, va, out, sizeof(*out)) == 0;
}

bool read_u64(pid_t pid, uint64_t va, uint64_t *out) {
  return onion_proc_copyout(pid, va, out, sizeof(*out)) == 0;
}

} // namespace

void AgcSources::reset() {
  pid_ = -1;
  global_va_ = 0;
}

bool AgcSources::sample_ring(pid_t pid, uint64_t *count) {
  if (!count || pid <= 0)
    return false;

  uint32_t ring_size = 0;
  if (!read_u32(pid, kRingVa + kRingOffSize, &ring_size))
    return false;
  if (ring_size == 0 || ring_size >= kMaxRing)
    ring_size = kDefaultRing;

  uint32_t write_idx = 0;
  if (!read_u32(pid, kRingVa + kRingOffIdx, &write_idx))
    return false;

  const uint64_t entries = kRingVa + kRingOffEntries;
  uint64_t best = 0;
  bool any = false;

  auto read_slot = [&](uint32_t slot) -> bool {
    const uint64_t va =
        entries + static_cast<uint64_t>(slot) * kSlotBytes + kSlotCountOff;
    uint64_t v = 0;
    if (!read_u64(pid, va, &v) || v == 0)
      return false;
    if (!any || v > best)
      best = v;
    any = true;
    return true;
  };

  if (ring_size > kWalkAllLimit) {
    for (int n = 1; n <= 4; ++n) {
      const uint32_t slot =
          static_cast<uint32_t>((static_cast<int>(ring_size) +
                                 static_cast<int>(write_idx) - n) %
                                static_cast<int>(ring_size));
      (void)read_slot(slot);
    }
  } else {
    for (uint32_t i = 0; i < ring_size; ++i)
      (void)read_slot(i);
  }

  if (!any)
    return false;
  if (!logged_ring_) {
    LOG_DEBUG("fps: DCB ring pid=%d size=%u", static_cast<int>(pid), ring_size);
    logged_ring_ = true;
  }
  *count = best;
  return true;
}

bool AgcSources::sample_global(pid_t pid, uint64_t *count) {
  if (!count || pid <= 0)
    return false;
  if (pid_ != pid) {
    pid_ = pid;
    global_va_ = 0;
  }

  if (global_va_ == 0) {
    uint32_t handle = 0;
    if (kernel_dynlib_handle(pid, "libSceAgcDriver.sprx", &handle) != 0 ||
        handle == 0) {
      if (!logged_global_) {
        LOG_DEBUG("fps: libSceAgcDriver.sprx not in pid=%d",
                  static_cast<int>(pid));
        logged_global_ = true;
      }
      return false;
    }
    const intptr_t base = kernel_dynlib_mapbase_addr(pid, handle);
    if (base <= 0)
      return false;
    global_va_ = static_cast<uint64_t>(base) + kGlobalOff;
    LOG_DEBUG("fps: AgcDriver base=0x%lx counter=0x%lx",
              static_cast<unsigned long>(base),
              static_cast<unsigned long>(global_va_));
  }

  uint64_t v = 0;
  if (!read_u64(pid, global_va_, &v)) {
    global_va_ = 0;
    return false;
  }
  *count = v;
  return true;
}

} // namespace fps
} // namespace onion
