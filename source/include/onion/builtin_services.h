/* Copyright (C) 2025 OnionHEN / LightningMods */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <strings.h>

/* Built-in service policy. These values are not user-configurable. */
#define ONION_FTPSRV_PORT 1337u

static inline int onion_builtin_ftp_key(const char *key) {
  return key &&
         (strcasecmp(key, "ftpsrv") == 0 || strcasecmp(key, "ftpsrv-ps5") == 0);
}

static inline int onion_builtin_shadowmount_key(const char *key) {
  return key && strcasecmp(key, "shadowmountplus") == 0;
}

/* True for leftover 0.0.10 user-payload names that collide with built-ins.
 * Those files may still live under payloads/; autostart must not replace a
 * built-in instance that the user has opted into. */
static inline int onion_builtin_service_key_reserved(const char *key) {
  return onion_builtin_ftp_key(key) || onion_builtin_shadowmount_key(key);
}

#ifdef __cplusplus
}
#endif
