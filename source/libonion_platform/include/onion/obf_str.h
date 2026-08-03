/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Lightweight string obfuscation for sensitive user-facing toasts.
 *
 * Plaintext is not stored as C string literals. Blobs use:
 *   enc[i] = (plain[i] ^ key[i % klen]) + (i * 17 + 31)   (mod 256)
 * Key material matches shellui version/banner: base64 → SISTR0_I_SEE_YOU.
 * Regenerate blobs: python3 source/shellui/assets/encrypt_banner.py --all
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decode an obfuscated blob into out (NUL-terminated).
 * Returns 0 on success, -1 if out is too small or args are invalid.
 */
int onion_obf_decode(const uint8_t *enc, size_t n, char *out, size_t out_sz);

/**
 * Decode en/zh by active onion_notify language and show a debug toast.
 * Uses "%s" formatting so decoded text may contain '%' safely.
 * Wipes the temporary plain buffer after send.
 */
void onion_notify_debug_obf(const uint8_t *en, size_t en_n, const uint8_t *zh,
                            size_t zh_n);

/** Integrity gate failure toast (en + zh obfuscated). */
void onion_notify_debug_integrity_failed(void);

/** Beta redistribution notice toast (en + zh obfuscated). */
void onion_notify_debug_beta_redistrib(void);

#ifdef __cplusplus
}
#endif
