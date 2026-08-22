/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * The facade is the only util-facing boundary for the in-process FTP module.
 * It owns the serving thread and keeps UI/IPC code independent from the
 * third-party implementation details.
 */

#include "service_facade.hpp"

#include <onion/builtin_services.h>
#include <onion/payload.h>
#include <onion/platform.h>

#include "srv.h"

#include <pthread.h>
#include <stdint.h>

namespace {

struct FtpRuntime {
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_t thread = {};
  bool running = false;
  bool thread_created = false;
  uint16_t port = ONION_FTPSRV_PORT;
};

FtpRuntime g_runtime;

void *ftp_thread_main(void *arg) {
  const uint16_t port = *static_cast<const uint16_t *>(arg);
  const int result = ftp_serve(port, /*notify_user=*/1);

  pthread_mutex_lock(&g_runtime.mutex);
  g_runtime.running = false;
  pthread_mutex_unlock(&g_runtime.mutex);

  if (result == FTP_SERVE_BIND_FAILED) {
    LOG_ERROR("ftpsrv failed to bind TCP %u", static_cast<unsigned>(port));
  } else if (result < 0) {
    LOG_ERROR("ftpsrv stopped with error %d", result);
  } else {
    LOG_INFO("ftpsrv stopped on TCP %u", static_cast<unsigned>(port));
  }
  return nullptr;
}

bool valid_port(uint16_t port) { return port != 0; }

} // namespace

namespace onion::services {

bool FtpServiceFacade::start(uint16_t port) {
  if (!valid_port(port)) {
    LOG_ERROR("Refusing invalid FTP port %u", static_cast<unsigned>(port));
    return false;
  }

  stop();

  /* Starting the built-in service is an explicit request to take the FTP
   * listener. Stop a recorded same-name Payload first; those names remain
   * valid in the user Payload loader. The module itself writes no PID marker. */
  onion_payload_stop_by_title("ftpsrv");
  onion_payload_stop_by_title("ftpsrv-ps5");

  pthread_mutex_lock(&g_runtime.mutex);
  g_runtime.port = port;
  g_runtime.running = true;
  ftp_server_prepare();
  const int rc = pthread_create(&g_runtime.thread, nullptr, ftp_thread_main,
                                &g_runtime.port);
  if (rc == 0) {
    g_runtime.thread_created = true;
  } else {
    g_runtime.running = false;
  }
  pthread_mutex_unlock(&g_runtime.mutex);

  if (rc != 0) {
    LOG_ERROR("Failed to create ftpsrv thread: %d", rc);
    return false;
  }

  LOG_INFO("ftpsrv started on TCP %u", static_cast<unsigned>(port));
  return true;
}

void FtpServiceFacade::stop() {
  pthread_t thread = {};
  bool join = false;

  pthread_mutex_lock(&g_runtime.mutex);
  if (g_runtime.thread_created) {
    ftp_server_stop();
    thread = g_runtime.thread;
    join = true;
  }
  pthread_mutex_unlock(&g_runtime.mutex);

  if (join) {
    pthread_join(thread, nullptr);
    pthread_mutex_lock(&g_runtime.mutex);
    g_runtime.running = false;
    g_runtime.thread_created = false;
    pthread_mutex_unlock(&g_runtime.mutex);
  }
}

bool FtpServiceFacade::reconfigure(uint16_t port) {
  if (!valid_port(port)) {
    return false;
  }
  if (!running()) {
    pthread_mutex_lock(&g_runtime.mutex);
    g_runtime.port = port;
    pthread_mutex_unlock(&g_runtime.mutex);
    return true;
  }

  const uint16_t previous_port = this->port();
  stop();
  if (start(port)) {
    return true;
  }

  /* Keep a working listener if the requested port is occupied.  The desired
   * value remains in Settings and can be retried after the conflicting
   * service is removed; the in-process service itself is never left half
   * stopped by a failed reconfigure. */
  if (previous_port != port && !start(previous_port)) {
    LOG_ERROR("ftpsrv failed to restore TCP %u after reconfigure failure",
              static_cast<unsigned>(previous_port));
  }
  return false;
}

bool FtpServiceFacade::running() const {
  pthread_mutex_lock(&g_runtime.mutex);
  const bool value = g_runtime.running;
  pthread_mutex_unlock(&g_runtime.mutex);
  return value;
}

uint16_t FtpServiceFacade::port() const {
  pthread_mutex_lock(&g_runtime.mutex);
  const uint16_t value = g_runtime.port;
  pthread_mutex_unlock(&g_runtime.mutex);
  return value;
}

FtpServiceFacade &ftpService() {
  static FtpServiceFacade service;
  return service;
}

} // namespace onion::services
