/* Copyright (C) 2025 OnionHEN / LightningMods */

#include <onion/notify_i18n.h>

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

typedef struct {
  const char *key;
  const char *zh;
  const char *en;
} notify_translation_t;

/* Generated from source/i18n locale catalogs at build time. */
#include "notify_i18n_catalog.inc"

static atomic_int gLanguage = ATOMIC_VAR_INIT(ONION_NOTIFY_LANG_EN);

void onion_notify_set_language(onion_notify_language_t language) {
  if (language != ONION_NOTIFY_LANG_ZH_HANS &&
      language != ONION_NOTIFY_LANG_EN) {
    language = ONION_NOTIFY_LANG_EN;
  }
  atomic_store_explicit(&gLanguage, language, memory_order_relaxed);
}

onion_notify_language_t onion_notify_get_language(void) {
  return (onion_notify_language_t)atomic_load_explicit(&gLanguage,
                                                        memory_order_relaxed);
}

onion_notify_language_t onion_notify_resolve_language(int ui_language,
                                                       int system_language) {
  if (ui_language == 1) {
    return ONION_NOTIFY_LANG_ZH_HANS;
  }
  if (ui_language == 2) {
    return ONION_NOTIFY_LANG_EN;
  }

  /* PS5 language ids: 10=Traditional Chinese, 11=Simplified Chinese. */
  return system_language == 10 || system_language == 11
             ? ONION_NOTIFY_LANG_ZH_HANS
             : ONION_NOTIFY_LANG_EN;
}

void onion_notify_apply_ui_language(int ui_language, int system_language) {
  onion_notify_set_language(
      onion_notify_resolve_language(ui_language, system_language));
}

const char *onion_notify_tr(const char *key) {
  if (!key)
    return "";

  for (size_t i = 0; i < sizeof(kTranslations) / sizeof(kTranslations[0]); ++i) {
    if (strcmp(kTranslations[i].key, key) == 0) {
      return onion_notify_get_language() == ONION_NOTIFY_LANG_ZH_HANS
                 ? kTranslations[i].zh
                 : kTranslations[i].en;
    }
  }
  return key;
}
