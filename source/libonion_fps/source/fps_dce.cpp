/* Copyright (C) 2026 OnionHEN / LightningMods */
#include <onion/fps_dce.hpp>

#include <onion/log.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace onion {
namespace fps {
namespace {

/* IOC_IN | size=0x30 | group=0x82 | cmd=0x17 — PHU videoout_ioctl::sample */
constexpr unsigned long kDceIoctl = 0x80308217UL;
constexpr uint64_t kArg0 = 0x10000000AULL;
constexpr uint64_t kArg1 = 0x8000000000ULL;
constexpr size_t kOutBytes = 0x70;
constexpr size_t kCountOff = 8; /* PHU: outbuf+8 is the flip counter */

} // namespace

DceSource::~DceSource() { close(); }

void DceSource::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool DceSource::open() {
  if (unavailable_)
    return false;
  if (fd_ >= 0)
    return true;

  fd_ = ::open("/dev/dce", O_RDWR);
  if (fd_ < 0) {
    const int err = errno;
    if (err == EPERM || err == EACCES || err == ENOENT)
      unavailable_ = true;
    if (!logged_fail_) {
      LOG_WARN("fps: open /dev/dce failed: %s", std::strerror(err));
      logged_fail_ = true;
    }
    return false;
  }
  LOG_INFO("fps: /dev/dce opened (scanout)");
  return true;
}

bool DceSource::sample(uint64_t *count) {
  if (!count)
    return false;
  if (fd_ < 0 && !open())
    return false;

  unsigned char out[kOutBytes]{};
  uint64_t arg[3];
  arg[0] = kArg0;
  arg[1] = kArg1;
  arg[2] = reinterpret_cast<uint64_t>(out);

  if (ioctl(fd_, kDceIoctl, arg) < 0) {
    const int err = errno;
    if (err == EBADF) {
      close();
      return false;
    }
    if (!logged_fail_) {
      LOG_WARN("fps: ioctl(0x80308217) failed: %s", std::strerror(err));
      logged_fail_ = true;
    }
    if (err == EPERM || err == EACCES)
      unavailable_ = true;
    return false;
  }
  std::memcpy(count, out + kCountOff, sizeof(*count));
  return true;
}

} // namespace fps
} // namespace onion
