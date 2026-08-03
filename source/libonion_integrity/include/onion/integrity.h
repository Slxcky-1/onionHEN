/* Copyright (C) 2025 OnionHEN
 *
 * ELF self-integrity (Ed25519 over the executable PT_LOAD).
 * Design mirrors kylin-core's self-check; namespaced for OnionHEN.
 *
 * Enable:  -DONION_ENABLE_ELF_PROTECTION=ON + public key
 * Disable: -DONION_ENABLE_ELF_PROTECTION=OFF  (verify is a no-op)
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sentinel returned by onion_self_integrity_status() for an intact image.
 * Compare against this value — do not treat the result as a boolean.
 */
#define ONION_SELF_INTEGRITY_OK 0x6f686e31u /* "ohn1" */

/**
 * Full Ed25519 verify of the protected executable range in memory.
 * When ELF protection is disabled at build time, always returns 0.
 *
 * @return 0 if intact, -1 if tampered / misconfigured.
 */
int onion_self_integrity_verify(void);

/**
 * Verify a signed ELF image buffer (e.g. embedded daemon.elf) before launch.
 * Same Ed25519 scheme as post-link sign-elf. No-op (returns 0) when protection
 * is compiled out.
 *
 * @return 0 if intact, -1 if tampered / misconfigured.
 */
int onion_elf_verify_signed_image(const void *elf, size_t size);

/**
 * Cached integrity verdict (lazy first check). Background monitor refreshes it.
 * @return ONION_SELF_INTEGRITY_OK if intact, any other value if bad.
 */
unsigned onion_self_integrity_status(void);

/**
 * Start a detached thread that re-verifies on a jittered interval.
 * No-op when protection is disabled.
 */
void onion_self_integrity_start_monitor(void);

#ifdef __cplusplus
}
#endif
