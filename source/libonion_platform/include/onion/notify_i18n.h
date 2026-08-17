/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Process-local localization for user-facing notification text.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum onion_notify_language {
  ONION_NOTIFY_LANG_ZH_HANS = 0,
  ONION_NOTIFY_LANG_EN = 1,
  ONION_NOTIFY_LANG_AR = 2,
} onion_notify_language_t;

/** Select the resolved language used by plain and rich notifications. */
void onion_notify_set_language(onion_notify_language_t language);

/** Return the currently selected notification language. */
onion_notify_language_t onion_notify_get_language(void);

/**
 * Resolve the shared toolbox language setting.
 * ui_language: 0=system, 1=zh-Hans, 2=en, 3=ar.
 * system_language is the value returned for SCE_SYSTEM_SERVICE_PARAM_ID_LANG
 * (10/11=Chinese, 21=Arabic; anything else follows English).
 */
onion_notify_language_t onion_notify_resolve_language(int ui_language,
                                                       int system_language);

/** Select a language from the shared setting and PS5 system language. */
void onion_notify_apply_ui_language(int ui_language, int system_language);

/** Translate a stable notification key; unknown keys are returned unchanged. */
const char *onion_notify_tr(const char *key);

#ifdef __cplusplus
}
#endif
