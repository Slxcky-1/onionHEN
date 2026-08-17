/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "toolbox_i18n.hpp"
#include <onion/notify_i18n.h>

#ifndef ONION_HOST_TEST
#include "external_symbols.hpp"
#endif

#include <cstdio>
#include <cstring>

namespace toolbox_i18n {
namespace {

#include "toolbox_i18n_catalog.inc"

const Entry *find_entry(const char *key) {
  if (!key)
    return nullptr;
  for (const Entry &e : kTable) {
    if (std::strcmp(e.key, key) == 0)
      return &e;
  }
  return nullptr;
}

Lang lang_from_notify(onion_notify_language_t language) {
  if (language == ONION_NOTIFY_LANG_ZH_HANS)
    return Lang::ZhHans;
  if (language == ONION_NOTIFY_LANG_AR)
    return Lang::Ar;
  return Lang::En;
}

onion_notify_language_t notify_from_lang(Lang lang) {
  if (lang == Lang::ZhHans)
    return ONION_NOTIFY_LANG_ZH_HANS;
  if (lang == Lang::Ar)
    return ONION_NOTIFY_LANG_AR;
  return ONION_NOTIFY_LANG_EN;
}

Lang lang_from_ui_value(int ui_lang) {
  if (ui_lang == 2)
    return Lang::En;
  if (ui_lang == 3)
    return Lang::Ar;
  return Lang::ZhHans;
}

const char *locale_id_for_lang(Lang lang) {
  if (lang == Lang::ZhHans)
    return "zh-Hans";
  if (lang == Lang::Ar)
    return "ar";
  return "en";
}

int locale_index_for_lang(Lang lang) {
  const char *id = locale_id_for_lang(lang);
  for (int i = 0; i < I18N_LOCALE_COUNT; ++i) {
    if (std::strcmp(kI18nLocaleIds[i], id) == 0)
      return i;
  }
  return kI18nLocaleFallback;
}

} // namespace

Lang active_lang() { return lang_from_notify(onion_notify_get_language()); }

int active_ui_lang_value() {
  const Lang lang = active_lang();
  if (lang == Lang::En)
    return 2;
  if (lang == Lang::Ar)
    return 3;
  return 1;
}

void set_lang(Lang lang) {
  if (lang != Lang::ZhHans && lang != Lang::En && lang != Lang::Ar)
    lang = Lang::ZhHans;
  onion_notify_set_language(notify_from_lang(lang));
}

void apply_ui_lang(int ui_lang) {
  set_lang(lang_from_ui_value(ui_lang));
}

void apply_system_or_ui_lang(int ui_lang) {
  if (ui_lang != 0) {
    set_lang(lang_from_ui_value(ui_lang));
    return;
  }

  int system_language = 1;
#ifndef ONION_HOST_TEST
  if (sceSystemServiceParamGetInt)
    (void)sceSystemServiceParamGetInt(1, &system_language);
#endif
  set_lang(lang_from_notify(onion_notify_resolve_language(0, system_language)));
}

const char *tr(const char *key) {
  const Entry *e = find_entry(key);
  if (!e)
    return key ? key : "";
  return e->text[locale_index_for_lang(active_lang())];
}

std::string formatv(const char *key, va_list ap) {
  const char *fmt = tr(key);
  va_list measure;
  va_copy(measure, ap);
  const int needed = std::vsnprintf(nullptr, 0, fmt, measure);
  va_end(measure);
  if (needed < 0)
    return {};
  std::string out(static_cast<size_t>(needed), '\0');
  std::vsnprintf(out.data(), static_cast<size_t>(needed) + 1, fmt, ap);
  return out;
}

std::string format(const char *key, ...) {
  va_list ap;
  va_start(ap, key);
  std::string out = formatv(key, ap);
  va_end(ap);
  return out;
}

} // namespace toolbox_i18n
