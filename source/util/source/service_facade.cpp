/* Copyright (C) 2025 OnionHEN / LightningMods
 * Stable OnionHEN service surface over independently running third-party ELFs.
 */

#include "service_facade.hpp"

#include <onion/builtin_services.h>
#include <onion/ipc_server.hpp>
#include <onion/payload.h>
#include <onion/platform.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

extern uint8_t ftpsrv_start[];
extern const unsigned int ftpsrv_size;
extern uint8_t shadowmount_start[];
extern const unsigned int shadowmount_size;

namespace onion::services {
namespace {

constexpr const char *kShadowControlSocket =
    "/system_tmp/onionhen/ipc/shadowmountplus_service";

pid_t launchShadowMount() {
  return onion_payload_launch_runtime("shadowmountplus", shadowmount_start,
                                      shadowmount_size, "shadowmountplus.elf",
                                      nullptr);
}

} // namespace

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

bool ShadowMountFacade::sendControl(const char *command) const {
  if (!command || !command[0])
    return false;

  const int fd = onion::ipc_unix_connect(kShadowControlSocket);
  if (fd < 0)
    return false;

  timeval timeout{2, 0};
  (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  char request[64];
  const int request_size = std::snprintf(request, sizeof(request), "%s\n", command);
  if (request_size <= 0 || static_cast<size_t>(request_size) >= sizeof(request) ||
      onion::ipc_network_send_full(fd, request, request_size) != request_size) {
    close(fd);
    return false;
  }

  char response[64]{};
  ssize_t received;
  do {
    received = read(fd, response, sizeof(response) - 1);
  } while (received < 0 && errno == EINTR);
  close(fd);
  return received >= 3 && std::strncmp(response, "OK ", 3) == 0;
}

bool ShadowMountFacade::running() const {
  return sendControl("status") || onion_payload_running("shadowmountplus");
}

bool ShadowMountFacade::ensureRunning() {
  if (sendControl("status"))
    return true;
  if (onion_payload_running("shadowmountplus")) {
    LOG_ERROR("ShadowMount+ is running without the OnionHEN control adapter");
    return false;
  }

  onion_payload_stop_by_title("shadowmountplus");
  if (launchShadowMount() <= 1)
    return false;

  for (int i = 0; i < 50; ++i) {
    if (sendControl("status"))
      return true;
    usleep(100000);
  }
  LOG_ERROR("ShadowMount+ control socket did not become ready");
  return false;
}

bool ShadowMountFacade::scanNow() {
  if (sendControl("scan"))
    return true;

  // Starting the resident scanner already performs a complete startup scan.
  return ensureRunning();
}

void ShadowMountFacade::stop() {
  if (!sendControl("shutdown"))
    onion_payload_stop_by_title("shadowmountplus");
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
