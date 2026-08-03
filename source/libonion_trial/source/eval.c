#include "eval.h"

#include <stdio.h>
#include <string.h>

#include "sha256.h"

static const char kDeviceFpNamespace[] = "onionhen-trial-v1";

static int64_t
i64_max(int64_t a, int64_t b) {
  return a > b ? a : b;
}

static size_t
hmac_body_size(void) {
  return offsetof(onion_trial_state_t, hmac);
}

/* HMAC-SHA256 (key may be any length; we only use 32-byte seal keys). */
static void
hmac_sha256(const unsigned char *key, size_t key_len, const unsigned char *data,
            size_t data_len, unsigned char out[ONION_TRIAL_HMAC_LEN]) {
  unsigned char k[64];
  unsigned char ipad[64];
  unsigned char opad[64];
  unsigned char inner[SHA256_BLOCK_SIZE];
  SHA256_CTX ctx;

  memset(k, 0, sizeof(k));
  if(key_len > sizeof(k)) {
    sha256_init(&ctx);
    sha256_update(&ctx, key, key_len);
    sha256_final(&ctx, k);
  } else {
    memcpy(k, key, key_len);
  }

  for(size_t i = 0; i < sizeof(k); ++i) {
    ipad[i] = (unsigned char)(k[i] ^ 0x36);
    opad[i] = (unsigned char)(k[i] ^ 0x5c);
  }

  sha256_init(&ctx);
  sha256_update(&ctx, ipad, sizeof(ipad));
  if(data_len > 0) {
    sha256_update(&ctx, data, data_len);
  }
  sha256_final(&ctx, inner);

  sha256_init(&ctx);
  sha256_update(&ctx, opad, sizeof(opad));
  sha256_update(&ctx, inner, sizeof(inner));
  sha256_final(&ctx, out);
}

int
onion_trial_device_fp(const char *serial,
                      unsigned char out[ONION_TRIAL_DEVICE_FP_LEN]) {
  SHA256_CTX ctx;
  unsigned char digest[SHA256_BLOCK_SIZE];

  if(serial == NULL || serial[0] == '\0' || out == NULL) {
    return -1;
  }
  sha256_init(&ctx);
  sha256_update(&ctx, (const unsigned char *)kDeviceFpNamespace,
                strlen(kDeviceFpNamespace));
  sha256_update(&ctx, (const unsigned char *)serial, strlen(serial));
  sha256_final(&ctx, digest);
  memcpy(out, digest, ONION_TRIAL_DEVICE_FP_LEN);
  return 0;
}

int
onion_trial_state_sign(const onion_beta_seal_t *seal,
                       onion_trial_state_t *state) {
  if(seal == NULL || state == NULL) {
    return -1;
  }
  state->magic = ONION_TRIAL_MAGIC;
  state->version = ONION_TRIAL_VERSION;
  hmac_sha256(seal->state_key, ONION_TRIAL_STATE_KEY_LEN,
              (const unsigned char *)state, hmac_body_size(), state->hmac);
  return 0;
}

int
onion_trial_state_verify(const onion_beta_seal_t *seal,
                         const onion_trial_state_t *state) {
  unsigned char expect[ONION_TRIAL_HMAC_LEN];

  if(seal == NULL || state == NULL) {
    return -1;
  }
  if(state->magic != ONION_TRIAL_MAGIC || state->version != ONION_TRIAL_VERSION) {
    return -1;
  }
  hmac_sha256(seal->state_key, ONION_TRIAL_STATE_KEY_LEN,
              (const unsigned char *)state, hmac_body_size(), expect);
  if(memcmp(expect, state->hmac, ONION_TRIAL_HMAC_LEN) != 0) {
    return -1;
  }
  return 0;
}

int
onion_trial_state_matches(const onion_beta_seal_t *seal,
                          const unsigned char *device_fp,
                          const onion_trial_state_t *state) {
  if(seal == NULL || device_fp == NULL || state == NULL) {
    return 0;
  }
  if(state->magic != ONION_TRIAL_MAGIC || state->version != ONION_TRIAL_VERSION) {
    return 0;
  }
  if(strncmp(state->build_id, seal->build_id, ONION_TRIAL_BUILD_ID_MAX) != 0) {
    return 0;
  }
  if(memcmp(state->device_fp, device_fp, ONION_TRIAL_DEVICE_FP_LEN) != 0) {
    return 0;
  }
  return 1;
}

const char *
onion_trial_code_str(onion_trial_code_t code) {
  switch(code) {
  case ONION_TRIAL_OK:
    return "ok";
  case ONION_TRIAL_CLOCK_EARLY:
    return "clock_early";
  case ONION_TRIAL_EXPIRED:
    return "expired";
  case ONION_TRIAL_ROLLBACK:
    return "rollback";
  case ONION_TRIAL_STICKY:
    return "sticky_expired";
  default:
    return "unknown";
  }
}

onion_trial_code_t
onion_trial_evaluate(const onion_beta_seal_t *seal, long long now,
                     const unsigned char *device_fp,
                     const onion_trial_state_t *in_state,
                     onion_trial_state_t *out_state, int *days_remaining_out) {
  int valid_in = 0;
  long long rem;

  if(seal == NULL || device_fp == NULL || out_state == NULL) {
    return ONION_TRIAL_EXPIRED;
  }

  memset(out_state, 0, sizeof(*out_state));
  out_state->magic = ONION_TRIAL_MAGIC;
  out_state->version = ONION_TRIAL_VERSION;
  snprintf(out_state->build_id, sizeof(out_state->build_id), "%s",
           seal->build_id);
  memcpy(out_state->device_fp, device_fp, ONION_TRIAL_DEVICE_FP_LEN);

  if(in_state != NULL &&
     onion_trial_state_matches(seal, device_fp, in_state)) {
    valid_in = 1;
    out_state->last_seen = in_state->last_seen;
    out_state->sticky_expired = in_state->sticky_expired;
  } else {
    out_state->last_seen = now;
    out_state->sticky_expired = 0;
  }

  rem = seal->not_after - now;
  if(days_remaining_out != NULL) {
    if(rem <= 0) {
      *days_remaining_out = 0;
    } else {
      *days_remaining_out = (int)((rem + 86399LL) / 86400LL);
    }
  }

  if(now < seal->not_before - seal->skew_sec) {
    return ONION_TRIAL_CLOCK_EARLY;
  }

  if(now > seal->not_after) {
    out_state->sticky_expired = 1;
    out_state->last_seen = i64_max(out_state->last_seen, now);
    return ONION_TRIAL_EXPIRED;
  }

  if(out_state->sticky_expired) {
    return ONION_TRIAL_STICKY;
  }

  if(valid_in && now + seal->skew_sec < out_state->last_seen) {
    return ONION_TRIAL_ROLLBACK;
  }

  out_state->last_seen = i64_max(out_state->last_seen, now);
  return ONION_TRIAL_OK;
}
