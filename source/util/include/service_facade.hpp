/* Copyright (C) 2025 OnionHEN / LightningMods */

#pragma once

#include <stdint.h>

namespace onion::services {

class FtpServiceFacade {
public:
  /** Start the in-process FTP module on @port. */
  bool start(uint16_t port);
  /** Stop the current session; idempotent. */
  void stop();
  /** Restart on a new port when the service is already running. */
  bool reconfigure(uint16_t port);
  bool running() const;
  uint16_t port() const;
};

FtpServiceFacade &ftpService();

} // namespace onion::services
