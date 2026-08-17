/* Copyright (C) 2025 OnionHEN / LightningMods */
#pragma once

#include <cstdint>

void NotifyRemotePlayPairingCountdown(uint32_t seconds_remaining);
void NotifyRemotePlayPairingTimedOut();
void NotifyRemotePlayPaired();
