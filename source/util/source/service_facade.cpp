/* Copyright (C) 2025 OnionHEN / LightningMods
 * Stable OnionHEN service surface over independently running third-party ELFs.
 */

#include "service_facade.hpp"

#include <onion/builtin_services.h>
#include <onion/payload.h>
#include <onion/platform.h>

#include <cstdio>

extern uint8_t ftpsrv_start[];
extern const unsigned int ftpsrv_size;
extern uint8_t shadowmount_start[];
extern const unsigned int shadowmount_size;

namespace onion::services {

bool FtpServiceFacade::start() {
  stop();
  char args[32];
  std::snprintf(args, sizeof(args), "-p %u",
                static_cast<unsigned>(ONION_FTPSRV_PORT));
  const pid_t pid = onion_payload_launch_runtime(
      "ftpsrv", ftpsrv_start, ftpsrv_size, "ftpsrv.elf", args);
  if (pid <= 1) {
    LOG_ERROR("Failed to start ftpsrv on TCP %u",
              static_cast<unsigned>(ONION_FTPSRV_PORT));
    return false;
  }
  LOG_INFO("ftpsrv started on TCP %u (pid=%d)",
           static_cast<unsigned>(ONION_FTPSRV_PORT), static_cast<int>(pid));
  return true;
}

void FtpServiceFacade::stop() { onion_payload_stop_by_title("ftpsrv"); }

bool FtpServiceFacade::running() const {
  return onion_payload_running("ftpsrv");
}

bool ShadowMountFacade::start() {
  stop();
  const pid_t pid = onion_payload_launch_runtime(
      "shadowmountplus", shadowmount_start, shadowmount_size,
      "shadowmountplus.elf", nullptr);
  if (pid <= 1) {
    LOG_ERROR("Failed to start ShadowMount+");
    return false;
  }
  LOG_INFO("ShadowMount+ started (pid=%d)", static_cast<int>(pid));
  return true;
}

void ShadowMountFacade::stop() {
  onion_payload_stop_by_title("shadowmountplus");
}

bool ShadowMountFacade::running() const {
  return onion_payload_running("shadowmountplus");
}

FtpServiceFacade &ftpService() {
  static FtpServiceFacade service;
  return service;
}

ShadowMountFacade &shadowMountService() {
  static ShadowMountFacade service;
  return service;
}

} // namespace onion::services
