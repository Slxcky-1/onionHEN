/* Copyright (C) 2025 OnionHEN / LightningMods */

#include <onion/notify_i18n.h>

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

/* Generated from source/i18n locale catalogs at build time. */
#include "notify_i18n_catalog.inc"

static atomic_int gLanguage = ATOMIC_VAR_INIT(ONION_NOTIFY_LANG_EN);

static int language_is_known(onion_notify_language_t language) {
  return language >= ONION_NOTIFY_LANG_ZH_HANS &&
         language <= ONION_NOTIFY_LANG_DE;
}

static const char *locale_id_for_language(onion_notify_language_t language) {
  switch (language) {
  case ONION_NOTIFY_LANG_ZH_HANS:
    return "zh-Hans";
  case ONION_NOTIFY_LANG_AR:
    return "ar";
  case ONION_NOTIFY_LANG_ZH_HANT:
    return "zh-Hant";
  case ONION_NOTIFY_LANG_JA:
    return "ja";
  case ONION_NOTIFY_LANG_FR:
    return "fr";
  case ONION_NOTIFY_LANG_DE:
    return "de";
  case ONION_NOTIFY_LANG_EN:
  default:
    return "en";
  }
}

static int locale_index_for_language(onion_notify_language_t language) {
  const char *id = locale_id_for_language(language);
  int i;

  for (i = 0; i < I18N_LOCALE_COUNT; ++i) {
    if (strcmp(kI18nLocaleIds[i], id) == 0)
      return i;
  }
  return kI18nLocaleFallback;
}

void onion_notify_set_language(onion_notify_language_t language) {
  if (!language_is_known(language))
    language = ONION_NOTIFY_LANG_EN;
  atomic_store_explicit(&gLanguage, language, memory_order_relaxed);
}

onion_notify_language_t onion_notify_get_language(void) {
  return (onion_notify_language_t)atomic_load_explicit(&gLanguage,
                                                        memory_order_relaxed);
}

onion_notify_language_t onion_notify_resolve_language(int ui_language,
                                                       int system_language) {
  switch (ui_language) {
  case 1:
    return ONION_NOTIFY_LANG_ZH_HANS;
  case 2:
    return ONION_NOTIFY_LANG_EN;
  case 3:
    return ONION_NOTIFY_LANG_AR;
  case 4:
    return ONION_NOTIFY_LANG_ZH_HANT;
  case 5:
    return ONION_NOTIFY_LANG_JA;
  case 6:
    return ONION_NOTIFY_LANG_FR;
  case 7:
    return ONION_NOTIFY_LANG_DE;
  default:
    break;
  }

  /* SCE_SYSTEM_SERVICE_PARAM_ID_LANG */
  switch (system_language) {
  case 0: /* Japanese */
    return ONION_NOTIFY_LANG_JA;
  case 2:  /* French */
  case 22: /* French (Canada) */
    return ONION_NOTIFY_LANG_FR;
  case 4: /* German */
    return ONION_NOTIFY_LANG_DE;
  case 10: /* Traditional Chinese */
    return ONION_NOTIFY_LANG_ZH_HANT;
  case 11: /* Simplified Chinese */
    return ONION_NOTIFY_LANG_ZH_HANS;
  case 21: /* Arabic */
    return ONION_NOTIFY_LANG_AR;
  default:
    return ONION_NOTIFY_LANG_EN;
  }
}

void onion_notify_apply_ui_language(int ui_language, int system_language) {
  onion_notify_set_language(
      onion_notify_resolve_language(ui_language, system_language));
}

const char *onion_notify_tr(const char *key) {
  const int idx = locale_index_for_language(onion_notify_get_language());
  size_t i;

  if (!key)
    return "";

  for (i = 0; i < sizeof(kTranslations) / sizeof(kTranslations[0]); ++i) {
    if (strcmp(kTranslations[i].key, key) == 0)
      return kTranslations[i].text[idx];
  }
  return key;
}
