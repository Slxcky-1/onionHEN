/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Lightweight i18n for dynamic toolbox XML (zh-Hans / en / ar).
 */
#pragma once

#include <cstdarg>
#include <string>
#include <string_view>

namespace toolbox_i18n {

/** Runtime language after resolving the stored UI language setting. */
enum class Lang : int {
  ZhHans = 0,
  En = 1,
  Ar = 2,
};

/** Active language for tr() (from settings or explicit set). */
Lang active_lang();

/** Active resolved language as an explicit UI language setting value. */
int active_ui_lang_value();

/**
 * Apply an explicit UI language setting value.
 * 1=zh-Hans, 2=en, 3=ar. Invalid values fall back to zh-Hans.
 */
void apply_ui_lang(int ui_lang);

/**
 * Apply the stored UI language setting.
 * 0=system, 1=zh-Hans, 2=en, 3=ar. This is a configuration boundary: system is
 * queried here once, then XML and notifications reuse the resolved language.
 */
void apply_system_or_ui_lang(int ui_lang);

/** Override without persisting (tests / temporary). */
void set_lang(Lang lang);

/**
 * Look up a UI string by stable key.
 * Missing key returns the key itself (visible failure).
 */
const char *tr(const char *key);

/** Convenience: tr(key) as std::string. */
inline std::string trs(const char *key) { return tr(key); }

/** Lookup tr(key) and apply printf placeholders. */
std::string formatv(const char *key, va_list ap);
std::string format(const char *key, ...);

} // namespace toolbox_i18n
