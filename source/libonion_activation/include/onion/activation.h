/* Copyright (C) 2025 OnionHEN
 *
 * Temporary beta / internal-test activation gate (Ed25519 certificate only).
 *
 * Self-contained under source/libonion_activation/ — remove later without
 * touching core product logic.
 *
 * Enable:  -DONION_ENABLE_BETA_ACTIVATION=ON  (default while in beta)
 * Disable: -DONION_ENABLE_BETA_ACTIVATION=OFF then delete this tree.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ONION_ACTIVATION_DIR
#define ONION_ACTIVATION_DIR "/data/OnionHEN/activation"
#endif
#ifndef ONION_ACTIVATION_LICENSE_PATH
#define ONION_ACTIVATION_LICENSE_PATH ONION_ACTIVATION_DIR "/license.json"
#endif

/** "sha256:" + 64 hex + NUL */
#define ONION_ACTIVATION_DEVICE_ID_LENGTH 72
/** First 32 hex chars of the device hash (for display / STATUS). */
#define ONION_ACTIVATION_DEVICE_CODE_LENGTH 33

typedef struct {
  int activated;
  char device_id[ONION_ACTIVATION_DEVICE_ID_LENGTH];
  char device_code[ONION_ACTIVATION_DEVICE_CODE_LENGTH];
  char reason[160];
  char license_id[128];
  char subject[128];
  long long expires_at;
} onion_activation_status_t;

/**
 * SHA-256 device id: "sha256:<hex>" from hardware serial (OpenPsId fallback).
 * @return 0 on success, -1 on failure.
 */
int onion_activation_get_device_id(char *out, size_t out_size);

/**
 * Fill status (always populates device_id/device_code when possible).
 * @return 0 if activated, -1 if not (or error — see reason).
 */
int onion_activation_get_status(onion_activation_status_t *status);

/** @return 1 if a valid Ed25519 license is installed, else 0. */
int onion_activation_is_active(void);

/**
 * Install an Ed25519-signed license.json body.
 * On success writes to ONION_ACTIVATION_LICENSE_PATH.
 * @return 0 on success, -1 on failure (err filled when provided).
 */
int onion_activation_install_license_json(const char *json, char *err,
                                          size_t err_size);

/**
 * Blocking gate for daemon startup.
 * If already activated, returns 0 immediately.
 * Otherwise shows notifications with the device id, listens on TCP for
 * LICENSE / STATUS, and polls drop paths until a valid certificate is installed.
 *
 * @return 0 when activated, -1 on fatal error (cannot read device id, etc.).
 */
int onion_activation_gate(void);

#ifdef __cplusplus
}
#endif
