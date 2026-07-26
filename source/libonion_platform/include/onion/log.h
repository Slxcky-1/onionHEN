/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * OnionHEN logging.
 *
 * Sinks: klog (volatile, read live from a PC) + an optional bounded file.
 * stdout is also written when the process has one.
 *
 * Two independent gates decide whether a record is emitted:
 *
 *   compile-time  ONION_LOG_COMPILE_LEVEL — records above this level generate
 *                 no code at all. The level test is a constant expression, so
 *                 the arguments are never even evaluated: a
 *                 LOG_TRACE("%s", expensive()) costs nothing in a release
 *                 build rather than costing a call whose result is discarded.
 *
 *   run-time      onion_log_set_level() — lets a user raise verbosity on a
 *                 retail unit from config.ini without reflashing a payload.
 *                 Only meaningful for levels that survived the compile gate.
 *
 * Threading: onion_log_write() is safe to call from any thread. It is NOT
 * async-signal-safe (it takes a lock and formats into a shared buffer); a
 * fault handler must use onion_log_emergency() instead.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ONION_LOG_OFF = 0,
  ONION_LOG_ERROR = 1, /* the operation failed and the user is affected */
  ONION_LOG_WARN = 2,  /* recovered, degraded, or a retry is coming */
  ONION_LOG_INFO = 3,  /* lifecycle milestones: started, injected, loaded */
  ONION_LOG_DEBUG = 4, /* decisions and state transitions, for bug reports */
  ONION_LOG_TRACE = 5, /* per-item / per-frame detail; noisy by design */
  /*
   * Not a level. Without a negative enumerator the compiler is free to give
   * this enum an unsigned underlying type, and a negative value cast into it
   * would wrap to a huge positive one — clamping "too low" up to TRACE, the
   * loudest setting, instead of down to OFF.
   */
  ONION_LOG__FORCE_SIGNED = -1,
} onion_log_level;

/*
 * Highest level compiled in. Release keeps INFO and above so a user-supplied
 * log still shows what the payload did, while DEBUG/TRACE — the bulk of the
 * volume — vanish entirely.
 *
 * Override per target with -DONION_LOG_COMPILE_LEVEL=ONION_LOG_DEBUG.
 */
#ifndef ONION_LOG_COMPILE_LEVEL
#ifdef NDEBUG
#define ONION_LOG_COMPILE_LEVEL ONION_LOG_INFO
#else
#define ONION_LOG_COMPILE_LEVEL ONION_LOG_TRACE
#endif
#endif

/** Default cap on the file sink before it rotates. */
#define ONION_LOG_DEFAULT_MAX_BYTES (1024u * 1024u)

/*
 * Current runtime threshold. Read directly by the macros so the common
 * "record is disabled" path is a load and a compare, with no call.
 * Deliberately not atomic: a racing set_level may cost one record its old
 * verdict, which is harmless, and it keeps the hot path free of barriers.
 */
extern volatile int onion_log_runtime_level;

/** Configure tag and optional file sink. Pass NULL path to disable the file. */
void onion_log_configure(const char *tag, const char *log_path);

/** Bound the file sink. 0 restores ONION_LOG_DEFAULT_MAX_BYTES. */
void onion_log_set_max_bytes(size_t max_bytes);

void onion_log_set_level(onion_log_level level);
onion_log_level onion_log_get_level(void);

/** "off"/"error"/"warn"/"info"/"debug"/"trace", case-insensitive. */
bool onion_log_level_from_name(const char *name, onion_log_level *out);
const char *onion_log_level_name(onion_log_level level);

/** Emit a record. Prefer the LOG_* macros, which apply both gates first. */
void onion_log_write(onion_log_level level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * Fault-handler path: formats onto the stack and writes straight to the sinks
 * with no locking and no allocation, so it stays usable when the process is
 * already dying and when the log lock may be held by the faulting thread.
 * Bypasses both gates — a crash is always worth recording.
 */
void onion_log_emergency(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/** Close the file sink. */
void onion_log_shutdown(void);

/*
 * The compile-time test comes first and is a constant expression, so for a
 * disabled level the whole statement — including the arguments — is discarded
 * before the runtime load is ever reached.
 */
#define ONION_LOG_AT(level, ...)                                               \
  do {                                                                         \
    if ((level) <= ONION_LOG_COMPILE_LEVEL &&                                  \
        (int)(level) <= onion_log_runtime_level) {                             \
      onion_log_write((level), __VA_ARGS__);                                   \
    }                                                                          \
  } while (0)

#define LOG_ERROR(...) ONION_LOG_AT(ONION_LOG_ERROR, __VA_ARGS__)
#define LOG_WARN(...) ONION_LOG_AT(ONION_LOG_WARN, __VA_ARGS__)
#define LOG_INFO(...) ONION_LOG_AT(ONION_LOG_INFO, __VA_ARGS__)
#define LOG_DEBUG(...) ONION_LOG_AT(ONION_LOG_DEBUG, __VA_ARGS__)
#define LOG_TRACE(...) ONION_LOG_AT(ONION_LOG_TRACE, __VA_ARGS__)

/**
 * Historical entry point, used by ~600 existing call sites. Treated as INFO:
 * those sites predate levels and are mostly lifecycle reporting, so silently
 * demoting them would strip a release log of its content.
 *
 * New code should pick an explicit level.
 */
void OnionHEN_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif
