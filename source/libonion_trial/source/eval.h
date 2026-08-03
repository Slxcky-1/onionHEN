#pragma once

#include <stddef.h>
#include <stdint.h>

#define ONION_TRIAL_BUILD_ID_MAX 32
#define ONION_TRIAL_DEVICE_FP_LEN 16
#define ONION_TRIAL_HMAC_LEN 32
#define ONION_TRIAL_STATE_KEY_LEN 32
#define ONION_TRIAL_SKEW_DEFAULT 86400LL
#define ONION_TRIAL_MAGIC 0x4F485452u /* 'OHTR' */
#define ONION_TRIAL_VERSION 1u

typedef struct {
  long long not_before;
  long long not_after;
  long long skew_sec;
  char build_id[ONION_TRIAL_BUILD_ID_MAX];
  unsigned char state_key[ONION_TRIAL_STATE_KEY_LEN];
} onion_beta_seal_t;

#pragma pack(push, 1)
typedef struct {
  uint32_t magic;
  uint32_t version;
  char build_id[ONION_TRIAL_BUILD_ID_MAX];
  unsigned char device_fp[ONION_TRIAL_DEVICE_FP_LEN];
  int64_t last_seen;
  uint8_t sticky_expired;
  uint8_t reserved[7];
  unsigned char hmac[ONION_TRIAL_HMAC_LEN];
} onion_trial_state_t;
#pragma pack(pop)

typedef enum {
  ONION_TRIAL_OK = 0,
  ONION_TRIAL_CLOCK_EARLY = 1,
  ONION_TRIAL_EXPIRED = 2,
  ONION_TRIAL_ROLLBACK = 3,
  ONION_TRIAL_STICKY = 4,
} onion_trial_code_t;

/**
 * Pure trial decision (host-testable).
 *
 * @param in_state  prior sealed state, or NULL if missing/invalid
 * @param out_state always written (caller should persist on OK/EXPIRED/STICKY)
 */
onion_trial_code_t onion_trial_evaluate(const onion_beta_seal_t *seal,
                                        long long now,
                                        const unsigned char *device_fp,
                                        const onion_trial_state_t *in_state,
                                        onion_trial_state_t *out_state,
                                        int *days_remaining_out);

int onion_trial_state_sign(const onion_beta_seal_t *seal,
                           onion_trial_state_t *state);
int onion_trial_state_verify(const onion_beta_seal_t *seal,
                             const onion_trial_state_t *state);

/**
 * On-disk sealed blob (AES-256-CBC + outer HMAC-SHA256, encrypt-then-MAC).
 * Layout: magic[8]="OHNTRLV1" | iv[16] | ciphertext[padded] | mac[32]
 */
#define ONION_TRIAL_BLOB_MAGIC "OHNTRLV1"
#define ONION_TRIAL_BLOB_IV_LEN 16
#define ONION_TRIAL_BLOB_MAX_CT 128 /* covers padded onion_trial_state_t */
#define ONION_TRIAL_BLOB_MAX_SIZE                                              \
  (8 + ONION_TRIAL_BLOB_IV_LEN + ONION_TRIAL_BLOB_MAX_CT + ONION_TRIAL_HMAC_LEN)

/** Seal a signed state into an opaque on-disk blob. */
int onion_trial_state_encrypt(const onion_beta_seal_t *seal,
                              const onion_trial_state_t *state,
                              unsigned char *out, size_t out_cap,
                              size_t *out_len);

/** Unseal on-disk blob into state (verifies outer MAC + inner HMAC). */
int onion_trial_state_decrypt(const onion_beta_seal_t *seal,
                              const unsigned char *blob, size_t blob_len,
                              onion_trial_state_t *out);

/** device_fp = first 16 bytes of SHA256("onionhen-trial-v1" || serial). */
int onion_trial_device_fp(const char *serial,
                          unsigned char out[ONION_TRIAL_DEVICE_FP_LEN]);

/** True if in_state belongs to this seal + device (ignores hmac). */
int onion_trial_state_matches(const onion_beta_seal_t *seal,
                              const unsigned char *device_fp,
                              const onion_trial_state_t *state);

const char *onion_trial_code_str(onion_trial_code_t code);
