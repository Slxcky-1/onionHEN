/* Copyright (C) 2025 OnionHEN / LightningMods */

#pragma once

namespace onion::services {

class FtpServiceFacade {
public:
  bool start();
  void stop();
  bool running() const;
};

class ShadowMountFacade {
public:
  // Starts the resident scanner when needed. A new instance performs its
  // normal startup scan; an existing instance receives an in-process scan.
  bool scanNow();
  bool ensureRunning();
  bool running() const;
  void stop();

private:
  bool sendControl(const char *command) const;
};

FtpServiceFacade &ftpService();
ShadowMountFacade &shadowMountService();

} // namespace onion::services
