/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Localized debug notifications for Remote Play pairing.
 */

#include "remote_play_notifications.hpp"

#include "toolbox_i18n.hpp"

#include <onion/notify.h>

void NotifyRemotePlayPairingCountdown(uint32_t seconds_remaining) {
  onion_notify_debug(toolbox_i18n::tr("rp.notify.countdown"),
                     seconds_remaining);
}

void NotifyRemotePlayPairingTimedOut() {
  onion_notify_debug("%s", toolbox_i18n::tr("rp.notify.timeout"));
}

void NotifyRemotePlayPaired() {
  onion_notify_debug("%s", toolbox_i18n::tr("rp.notify.paired"));
}
