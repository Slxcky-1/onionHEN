#include <onion/trial.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <onion/log.h>
#include <onion/notify.h>

#include "crypto_util.h"
#include "device.h"
#include "eval.h"

/*
 * Compile-time beta seal (injected by CMake). No in-source defaults for the
 * state key — CMake generates one when not supplied.
 */
#ifndef ONION_BETA_NOT_BEFORE
#error "ONION_BETA_NOT_BEFORE is required"
#endif
#ifndef ONION_BETA_NOT_AFTER
#error "ONION_BETA_NOT_AFTER is required"
#endif
#ifndef ONION_BETA_BUILD_ID
#error "ONION_BETA_BUILD_ID is required"
#endif
#ifndef ONION_BETA_STATE_KEY_HEX
#error "ONION_BETA_STATE_KEY_HEX is required (64 hex chars)"
#endif

#ifndef ONION_BETA_SKEW_SEC
#define ONION_BETA_SKEW_SEC ONION_TRIAL_SKEW_DEFAULT
#endif

static int
load_seal(onion_beta_seal_t *seal) {
  if(seal == NULL) {
    return -1;
  }
  memset(seal, 0, sizeof(*seal));
  seal->not_before = (long long)ONION_BETA_NOT_BEFORE;
  seal->not_after = (long long)ONION_BETA_NOT_AFTER;
  seal->skew_sec = (long long)ONION_BETA_SKEW_SEC;
  snprintf(seal->build_id, sizeof(seal->build_id), "%s", ONION_BETA_BUILD_ID);
  if(onion_trial_hex_decode(ONION_BETA_STATE_KEY_HEX, seal->state_key,
                            ONION_TRIAL_STATE_KEY_LEN) < 0) {
    return -1;
  }
  if(seal->not_after <= seal->not_before) {
    return -1;
  }
  return 0;
}

static int
ensure_trial_dir(void) {
  struct stat st;

  if(stat(ONION_TRIAL_DIR, &st) == 0) {
    return S_ISDIR(st.st_mode) ? 0 : -1;
  }
  if(mkdir("/data", 0777) != 0 && errno != EEXIST) {
    /* best-effort */
  }
  if(mkdir("/data/OnionHEN", 0777) != 0 && errno != EEXIST) {
    /* best-effort */
  }
  if(mkdir(ONION_TRIAL_DIR, 0777) != 0 && errno != EEXIST) {
    return -1;
  }
  return 0;
}

static int
read_trial_state(const onion_beta_seal_t *seal, const char *path,
                 onion_trial_state_t *state) {
  FILE *fp;
  unsigned char blob[ONION_TRIAL_BLOB_MAX_SIZE];
  size_t n;

  if(seal == NULL || path == NULL || state == NULL) {
    return -1;
  }
  fp = fopen(path, "rb");
  if(fp == NULL) {
    return -1;
  }
  n = fread(blob, 1, sizeof(blob), fp);
  fclose(fp);
  if(n == 0) {
    return -1;
  }
  /* Encrypted blob (OHNTRLV1); reject legacy plaintext formats. */
  if(onion_trial_state_decrypt(seal, blob, n, state) < 0) {
    return -1;
  }
  return 0;
}

static int
write_trial_state(const char *path, const onion_beta_seal_t *seal,
                  onion_trial_state_t *state) {
  FILE *fp;
  onion_trial_state_t signed_state;
  unsigned char blob[ONION_TRIAL_BLOB_MAX_SIZE];
  size_t blob_len = 0;

  if(path == NULL || seal == NULL || state == NULL) {
    return -1;
  }
  signed_state = *state;
  if(onion_trial_state_sign(seal, &signed_state) < 0) {
    return -1;
  }
  if(onion_trial_state_encrypt(seal, &signed_state, blob, sizeof(blob),
                               &blob_len) < 0) {
    return -1;
  }
  if(ensure_trial_dir() < 0) {
    return -1;
  }
  fp = fopen(path, "wb");
  if(fp == NULL) {
    return -1;
  }
  if(fwrite(blob, 1, blob_len, fp) != blob_len) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  *state = signed_state;
  return 0;
}

static int
load_valid_state(const onion_beta_seal_t *seal, const unsigned char *device_fp,
                 onion_trial_state_t *out, int *have_out) {
  onion_trial_state_t raw;

  *have_out = 0;
  if(read_trial_state(seal, ONION_TRIAL_STATE_PATH, &raw) < 0) {
    LOG_WARN("[trial] trial.state missing/unreadable/decrypt failed; fresh");
    return 0;
  }
  if(!onion_trial_state_matches(seal, device_fp, &raw)) {
    LOG_INFO("[trial] trial.state build/device mismatch; new campaign");
    return 0;
  }
  *out = raw;
  *have_out = 1;
  return 0;
}

static void
fill_status(onion_trial_status_t *status, const onion_beta_seal_t *seal,
            onion_trial_code_t code, int days_remaining) {
  memset(status, 0, sizeof(*status));
  status->not_before = seal->not_before;
  status->not_after = seal->not_after;
  status->days_remaining = days_remaining;
  snprintf(status->build_id, sizeof(status->build_id), "%s", seal->build_id);
  status->active = (code == ONION_TRIAL_OK) ? 1 : 0;
  switch(code) {
  case ONION_TRIAL_OK:
    snprintf(status->reason, sizeof(status->reason), "OK (%d day(s) left)",
             days_remaining);
    break;
  case ONION_TRIAL_CLOCK_EARLY:
    snprintf(status->reason, sizeof(status->reason),
             "system clock too far before trial window");
    break;
  case ONION_TRIAL_EXPIRED:
    snprintf(status->reason, sizeof(status->reason), "beta trial expired");
    break;
  case ONION_TRIAL_ROLLBACK:
    snprintf(status->reason, sizeof(status->reason),
             "system clock rollback detected");
    break;
  case ONION_TRIAL_STICKY:
    snprintf(status->reason, sizeof(status->reason),
             "beta trial previously expired on this device");
    break;
  default:
    snprintf(status->reason, sizeof(status->reason), "trial rejected");
    break;
  }
}

int
onion_trial_get_status(onion_trial_status_t *status) {
  onion_beta_seal_t seal;
  onion_trial_state_t prior;
  onion_trial_state_t next;
  unsigned char device_fp[ONION_TRIAL_DEVICE_FP_LEN];
  char serial[256];
  int have_prior = 0;
  int days = 0;
  onion_trial_code_t code;
  long long now;

  if(status == NULL) {
    return -1;
  }
  memset(status, 0, sizeof(*status));
  if(load_seal(&seal) < 0) {
    snprintf(status->reason, sizeof(status->reason), "invalid beta seal");
    return -1;
  }
  if(onion_trial_device_serial(serial, sizeof(serial)) < 0 ||
     onion_trial_device_fp(serial, device_fp) < 0) {
    snprintf(status->reason, sizeof(status->reason), "device identity unavailable");
    status->not_before = seal.not_before;
    status->not_after = seal.not_after;
    snprintf(status->build_id, sizeof(status->build_id), "%s", seal.build_id);
    return -1;
  }
  (void)load_valid_state(&seal, device_fp, &prior, &have_prior);
  now = (long long)time(NULL);
  code = onion_trial_evaluate(&seal, now, device_fp, have_prior ? &prior : NULL,
                              &next, &days);
  fill_status(status, &seal, code, days);
  return code == ONION_TRIAL_OK ? 0 : -1;
}

int
onion_trial_is_active(void) {
  onion_trial_status_t status;
  return onion_trial_get_status(&status) == 0 ? 1 : 0;
}

int
onion_trial_gate_ex(int notify_ok) {
  onion_beta_seal_t seal;
  onion_trial_state_t prior;
  onion_trial_state_t next;
  unsigned char device_fp[ONION_TRIAL_DEVICE_FP_LEN];
  char serial[256];
  int have_prior = 0;
  int days = 0;
  onion_trial_code_t code;
  long long now;
  int should_write = 0;

  ensure_trial_dir();

  if(load_seal(&seal) < 0) {
    LOG_ERROR("[trial] invalid compile-time beta seal");
    onion_notify_debug("notify.trial.bad_seal");
    return -1;
  }

  if(onion_trial_device_serial(serial, sizeof(serial)) < 0 ||
     onion_trial_device_fp(serial, device_fp) < 0) {
    LOG_ERROR("[trial] cannot read device identity");
    onion_notify_debug("notify.trial.no_identity");
    return -1;
  }

  (void)load_valid_state(&seal, device_fp, &prior, &have_prior);
  now = (long long)time(NULL);
  code = onion_trial_evaluate(&seal, now, device_fp, have_prior ? &prior : NULL,
                              &next, &days);

  LOG_INFO("[trial] gate build_id=%s code=%s days_left=%d not_after=%lld",
           seal.build_id, onion_trial_code_str(code), days, seal.not_after);

  switch(code) {
  case ONION_TRIAL_OK:
    should_write = 1;
    if(notify_ok) {
      onion_notify_debug("notify.trial.days", days);
    }
    break;
  case ONION_TRIAL_EXPIRED:
    should_write = 1;
    onion_notify_debug(
        "notify.trial.expired");
    break;
  case ONION_TRIAL_STICKY:
    should_write = 1;
    onion_notify_debug(
        "notify.trial.expired");
    break;
  case ONION_TRIAL_ROLLBACK:
  case ONION_TRIAL_CLOCK_EARLY:
    onion_notify_debug(
        "notify.trial.clock");
    break;
  default:
    onion_notify_debug("notify.trial.expired");
    break;
  }

  if(should_write) {
    if(write_trial_state(ONION_TRIAL_STATE_PATH, &seal, &next) < 0) {
      LOG_WARN("[trial] failed to persist trial.state");
      /* Still allow OK path if persistence fails — clock anti-rollback weakens. */
    }
  }

  return code == ONION_TRIAL_OK ? 0 : -1;
}

int
onion_trial_gate(void) {
  return onion_trial_gate_ex(1);
}
