/* Copyright (C) 2025 OnionHEN / LightningMods */

#pragma once

/* Toolbox re-injection entry point (util -> crit daemon).
 * apply_rest_delay: true only for the first rest-cycle attempt so retries
 * do not re-sleep rest_mode.resume_reinject_delay_seconds. */
bool toolbox_reinject(bool rest_resume, bool apply_rest_delay = false);
