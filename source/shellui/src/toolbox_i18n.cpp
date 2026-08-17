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

struct Entry {
  const char *key;
  const char *zh;
  const char *en;
};

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

Lang lang_from_ui_value(int ui_lang) {
  return ui_lang == 2 ? Lang::En : Lang::ZhHans;
}

} // namespace

Lang active_lang() {
  return onion_notify_get_language() == ONION_NOTIFY_LANG_ZH_HANS
             ? Lang::ZhHans
             : Lang::En;
}

int active_ui_lang_value() { return active_lang() == Lang::En ? 2 : 1; }

void set_lang(Lang lang) {
  if (lang != Lang::ZhHans && lang != Lang::En)
    lang = Lang::ZhHans;
  onion_notify_set_language(lang == Lang::ZhHans ? ONION_NOTIFY_LANG_ZH_HANS
                                                  : ONION_NOTIFY_LANG_EN);
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
  set_lang(onion_notify_resolve_language(0, system_language) ==
                   ONION_NOTIFY_LANG_ZH_HANS
               ? Lang::ZhHans
               : Lang::En);
}

const char *tr(const char *key) {
  const Entry *e = find_entry(key);
  if (!e)
    return key ? key : "";
  return active_lang() == Lang::En ? e->en : e->zh;
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
