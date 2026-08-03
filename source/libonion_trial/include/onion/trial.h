/* Copyright (C) 2025 OnionHEN
 *
 * Temporary beta trial gate: build-time seal + local anti-rollback state.
 *
 * Self-contained under source/libonion_trial/ — remove later without
 * touching core product logic.
 *
 * Enable:  -DONION_ENABLE_BETA_TRIAL=ON  (default while in beta)
 * Disable: -DONION_ENABLE_BETA_TRIAL=OFF then delete this tree.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ONION_TRIAL_DIR
#define ONION_TRIAL_DIR "/data/OnionHEN/trial"
#endif
#ifndef ONION_TRIAL_STATE_PATH
#define ONION_TRIAL_STATE_PATH ONION_TRIAL_DIR "/trial.state"
#endif

/**
 * Runtime status for logging / diagnostics.
 * onion_trial_get_status returns 0 if trial is currently valid, -1 otherwise.
 */
typedef struct {
  int active;
  int days_remaining;
  long long not_before;
  long long not_after;
  char build_id[32];
  char reason[160];
} onion_trial_status_t;

int onion_trial_get_status(onion_trial_status_t *status);

/** @return 1 if the beta trial is currently valid, else 0. */
int onion_trial_is_active(void);

/**
 * Single-shot gate for daemon startup.
 * Validates the compile-time beta seal against wall clock and a sealed
 * per-device trial.state (anti clock-rollback).
 *
 * @return 0 when allowed to continue, -1 when the beta build must not run.
 */
int onion_trial_gate(void);

#ifdef __cplusplus
}
#endif
