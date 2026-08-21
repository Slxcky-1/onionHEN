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
  bool start();
  void stop();
  bool running() const;
};

FtpServiceFacade &ftpService();
ShadowMountFacade &shadowMountService();

} // namespace onion::services
