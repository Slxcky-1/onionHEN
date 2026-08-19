/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split. */


#include <onion/platform.h>
#include <onion/ready.h>
#include <onion/ipc_client.hpp>
#include <onion/settings.hpp>
#include <onion/toolbox_timing.h>
#include <msg.hpp>
#include "common_utils.h"
#include "util_toolbox.h"
#include <unistd.h>

bool enable_toolbox() {
    // Single client path into crit daemon (replaces hand-rolled Unix socket).
    // Wait briefly for crit socket / ready — bootstrap order is util then daemon.
    for (int wait = 0; wait <= 20; ++wait) {
      if (onion_ready_is_set(ONION_READY_DAEMON) || if_exists(CRIT_IPC_SOC)) {
        break;
      }
      if (wait == 20) {
        onion_notify(true, "notify.toolbox.load_failed");
        return false;
      }
      sleep(1);
    }
    return IPC_Client::getInstance(/*util=*/false).EnableToolbox();
}

/**
 * Re-request toolbox inject via crit daemon.
 * @param rest_resume  true only for real rest-mode recovery paths.
 *                     false for util restart / re-HEN reinject (no rest copy).
 *
 * The delay policy is driven by the rest_resume flag: only rest-mode recovery
 * waits the configured rest_mode.resume_reinject_delay_seconds (see
 * onion_toolbox_should_apply_rest_delay). The reinjection side effect itself
 * is owned by this function; when to trigger it is the rest_mode state
 * machine's decision.
 */
void toolbox_reinject(bool rest_resume) {
    LoadSettings();
    const onion::Settings cfg = g_settings.snapshot();

    if (rest_resume &&
        onion_toolbox_should_apply_rest_delay(true, cfg.rest_mode_delay_seconds)) {
        LOG_DEBUG(
            "rest resume delay %llu secs",
            static_cast<unsigned long long>(cfg.rest_mode_delay_seconds));
        sleep(static_cast<unsigned int>(cfg.rest_mode_delay_seconds));
        onion_notify(true,
                     "notify.rest.reactivating");
    } else {
        LOG_DEBUG("toolbox reinject (not rest resume)");
    }

    if (!enable_toolbox()) {
        onion_notify(true, "notify.toolbox.inject_failed");
    }
}
