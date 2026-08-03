#include "eval.h"

#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "sha256.h"

#define CBC 1
#define AES256 1
#include "mc4/aes.h"

/* PKCS#7 padded size (always at least one pad byte → next full block). */
#define STATE_PLAIN_LEN ((size_t)sizeof(onion_trial_state_t))
#define STATE_PADDED_LEN                                                       \
  (((STATE_PLAIN_LEN / AES_BLOCKLEN) + 1u) * AES_BLOCKLEN)

static void
sha256_label(const unsigned char *key, size_t key_len, const char *label,
             unsigned char out[32]) {
  SHA256_CTX ctx;

  sha256_init(&ctx);
  sha256_update(&ctx, key, key_len);
  sha256_update(&ctx, (const unsigned char *)label, strlen(label));
  sha256_final(&ctx, out);
}

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

static int
fill_random(unsigned char *out, size_t n) {
  int fd;
  size_t got = 0;

  if(out == NULL || n == 0) {
    return -1;
  }
  fd = open("/dev/urandom", O_RDONLY);
  if(fd >= 0) {
    while(got < n) {
      ssize_t r = read(fd, out + got, n - got);
      if(r <= 0) {
        break;
      }
      got += (size_t)r;
    }
    close(fd);
  }
  if(got == n) {
    return 0;
  }
  /* Weak fallback: mix time into remaining bytes (still better than fixed IV). */
  {
    long long t = (long long)time(NULL);
    unsigned char mix[8];
    memcpy(mix, &t, sizeof(mix) < sizeof(t) ? sizeof(mix) : sizeof(t));
    for(size_t i = got; i < n; ++i) {
      out[i] = (unsigned char)(mix[i % sizeof(mix)] ^ (unsigned char)(i * 17u));
    }
  }
  return 0;
}

static int
pkcs7_pad(unsigned char *buf, size_t plain_len, size_t padded_len) {
  unsigned char pad;

  if(buf == NULL || padded_len < plain_len || padded_len - plain_len > 255) {
    return -1;
  }
  pad = (unsigned char)(padded_len - plain_len);
  if(pad == 0) {
    return -1; /* caller must allocate room for a full block of padding */
  }
  for(size_t i = plain_len; i < padded_len; ++i) {
    buf[i] = pad;
  }
  return 0;
}

static int
pkcs7_unpad(const unsigned char *buf, size_t padded_len, size_t *plain_len_out) {
  unsigned char pad;
  size_t i;

  if(buf == NULL || plain_len_out == NULL || padded_len == 0 ||
     padded_len % AES_BLOCKLEN != 0) {
    return -1;
  }
  pad = buf[padded_len - 1];
  if(pad == 0 || pad > AES_BLOCKLEN || pad > padded_len) {
    return -1;
  }
  for(i = 0; i < pad; ++i) {
    if(buf[padded_len - 1 - i] != pad) {
      return -1;
    }
  }
  *plain_len_out = padded_len - pad;
  return 0;
}

int
onion_trial_state_encrypt(const onion_beta_seal_t *seal,
                          const onion_trial_state_t *state, unsigned char *out,
                          size_t out_cap, size_t *out_len) {
  unsigned char aes_key[32];
  unsigned char mac_key[32];
  unsigned char iv[ONION_TRIAL_BLOB_IV_LEN];
  unsigned char plain[STATE_PADDED_LEN];
  unsigned char mac[ONION_TRIAL_HMAC_LEN];
  struct AES_ctx ctx;
  size_t need;
  size_t off = 0;

  if(seal == NULL || state == NULL || out == NULL || out_len == NULL) {
    return -1;
  }
  if(STATE_PADDED_LEN > ONION_TRIAL_BLOB_MAX_CT) {
    return -1;
  }
  need = 8 + ONION_TRIAL_BLOB_IV_LEN + STATE_PADDED_LEN + ONION_TRIAL_HMAC_LEN;
  if(out_cap < need) {
    return -1;
  }

  memset(plain, 0, sizeof(plain));
  memcpy(plain, state, STATE_PLAIN_LEN);
  if(pkcs7_pad(plain, STATE_PLAIN_LEN, STATE_PADDED_LEN) < 0 ||
     fill_random(iv, sizeof(iv)) < 0) {
    return -1;
  }

  sha256_label(seal->state_key, ONION_TRIAL_STATE_KEY_LEN, "onion-trial-aes-v1",
               aes_key);
  sha256_label(seal->state_key, ONION_TRIAL_STATE_KEY_LEN, "onion-trial-mac-v1",
               mac_key);

  AES_init_ctx_iv(&ctx, aes_key, iv);
  AES_CBC_encrypt_buffer(&ctx, plain, STATE_PADDED_LEN);

  memcpy(out + off, ONION_TRIAL_BLOB_MAGIC, 8);
  off += 8;
  memcpy(out + off, iv, sizeof(iv));
  off += sizeof(iv);
  memcpy(out + off, plain, STATE_PADDED_LEN);
  off += STATE_PADDED_LEN;

  hmac_sha256(mac_key, sizeof(mac_key), out, off, mac);
  memcpy(out + off, mac, sizeof(mac));
  off += sizeof(mac);

  memset(aes_key, 0, sizeof(aes_key));
  memset(mac_key, 0, sizeof(mac_key));
  memset(plain, 0, sizeof(plain));
  *out_len = off;
  return 0;
}

int
onion_trial_state_decrypt(const onion_beta_seal_t *seal,
                          const unsigned char *blob, size_t blob_len,
                          onion_trial_state_t *out) {
  unsigned char aes_key[32];
  unsigned char mac_key[32];
  unsigned char expect[ONION_TRIAL_HMAC_LEN];
  unsigned char ct[STATE_PADDED_LEN];
  unsigned char iv[ONION_TRIAL_BLOB_IV_LEN];
  struct AES_ctx ctx;
  size_t body_len;
  size_t plain_len = 0;
  onion_trial_state_t state;

  if(seal == NULL || blob == NULL || out == NULL) {
    return -1;
  }
  /* magic + iv + at least one block + mac */
  if(blob_len < 8 + ONION_TRIAL_BLOB_IV_LEN + AES_BLOCKLEN + ONION_TRIAL_HMAC_LEN) {
    return -1;
  }
  if(memcmp(blob, ONION_TRIAL_BLOB_MAGIC, 8) != 0) {
    return -1;
  }
  body_len = blob_len - ONION_TRIAL_HMAC_LEN;
  if(body_len < 8 + ONION_TRIAL_BLOB_IV_LEN ||
     (body_len - 8 - ONION_TRIAL_BLOB_IV_LEN) != STATE_PADDED_LEN) {
    return -1;
  }

  sha256_label(seal->state_key, ONION_TRIAL_STATE_KEY_LEN, "onion-trial-mac-v1",
               mac_key);
  hmac_sha256(mac_key, sizeof(mac_key), blob, body_len, expect);
  if(memcmp(expect, blob + body_len, ONION_TRIAL_HMAC_LEN) != 0) {
    memset(mac_key, 0, sizeof(mac_key));
    return -1;
  }

  memcpy(iv, blob + 8, sizeof(iv));
  memcpy(ct, blob + 8 + ONION_TRIAL_BLOB_IV_LEN, STATE_PADDED_LEN);

  sha256_label(seal->state_key, ONION_TRIAL_STATE_KEY_LEN, "onion-trial-aes-v1",
               aes_key);
  AES_init_ctx_iv(&ctx, aes_key, iv);
  AES_CBC_decrypt_buffer(&ctx, ct, STATE_PADDED_LEN);

  if(pkcs7_unpad(ct, STATE_PADDED_LEN, &plain_len) < 0 ||
     plain_len != STATE_PLAIN_LEN) {
    memset(aes_key, 0, sizeof(aes_key));
    memset(mac_key, 0, sizeof(mac_key));
    memset(ct, 0, sizeof(ct));
    return -1;
  }
  memcpy(&state, ct, STATE_PLAIN_LEN);
  memset(aes_key, 0, sizeof(aes_key));
  memset(mac_key, 0, sizeof(mac_key));
  memset(ct, 0, sizeof(ct));

  if(onion_trial_state_verify(seal, &state) < 0) {
    return -1;
  }
  *out = state;
  return 0;
}
