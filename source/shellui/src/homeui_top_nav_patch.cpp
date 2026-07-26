/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include "homeui_top_nav_patch.hpp"

#if SHELLUI_HOMEUI_TOP_NAV_PATCH == 1

#include "defs.h"
#include "detour.h"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include <elf/nid/sha1.hpp>

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <string.h>

namespace {

constexpr unsigned char kHermesMagic[] = {0xc6, 0x1f, 0xbc, 0x03,
                                          0xc1, 0x03, 0x19, 0x1f};
constexpr unsigned char kRnpsMagic[] = {'R', 'N', 'P', 'S',
                                        'H', 'E', 'D', 'R'};

constexpr size_t kRnpsPayloadOffsetField = 0x1c;
constexpr size_t kRnpsFallbackPayloadOffset = 0xb20;
constexpr size_t kHbcVersionOffset = 0x08;
constexpr size_t kHbcSourceHashOffset = 0x0c;
constexpr size_t kHbcSourceHashSize = 20;
constexpr size_t kHbcFileLengthOffset = 0x20;
constexpr size_t kHbcFooterSha1Size = 20;
constexpr const char *kOnionHenTopNavIconPath =
    "/system_ex/vsh_asset/onionhen.png";

std::atomic<bool> g_homeui_top_nav_reload_pending{false};
std::atomic<bool> g_logged_non_homeui_skip{false};
std::atomic<bool> g_logged_unsupported_homeui_skip{false};
void (*g_react_button_set_icon_source_orig)(MonoObject *instance,
                                            MonoObject *source) = nullptr;
void (*g_react_button_set_inverted_icon_source)(MonoObject *instance,
                                                MonoObject *source) = nullptr;

struct HbcView {
  unsigned char *data;
  size_t size;
  size_t base_offset;
};

struct BytePatch {
  const char *name;
  size_t offset;
  const unsigned char *expected;
  const unsigned char *alternate_expected;
  const unsigned char *second_alternate_expected;
  const unsigned char *replacement;
  size_t size;
};

/*
 * Patch widths. Every firmware uses the same widths, so making them the array
 * bounds in HomeUiPatchBytes lets the compiler reject a mistyped blob. This
 * replaces the pairwise static_asserts the previous layout needed.
 */
constexpr size_t kIconOrderSize = 9;
constexpr size_t kFpsFactorySize = 5;
constexpr size_t kObjectTableValueSize = 2;
constexpr size_t kButtonBodySize = 77;

/* Per-firmware byte patterns. Field order matches the patch list below. */
struct HomeUiPatchBytes {
  unsigned char old_icon_order[kIconOrderSize];
  /* Prior strategy: [Search, Fps, Settings, Profile] — migrate away. */
  unsigned char legacy_fps_slot_icon_order[kIconOrderSize];
  unsigned char legacy_aliased_icon_order[kIconOrderSize];
  /* Target: [Search, ApplicationErrorEventTrigger, Settings, Profile]. */
  unsigned char app_error_icon_order[kIconOrderSize];
  unsigned char original_fps_factory[kFpsFactorySize];
  unsigned char legacy_aliased_fps_factory[kFpsFactorySize];
  unsigned char old_custom_icon_value[kObjectTableValueSize];
  unsigned char new_custom_icon_value[kObjectTableValueSize];
  unsigned char old_custom_title_value[kObjectTableValueSize];
  unsigned char old_fps_body_prefix[kButtonBodySize];
  /* Only some firmwares ever shipped an aliased button body. */
  bool has_legacy_button_body;
  unsigned char legacy_onion_hen_button_body[kButtonBodySize];
  unsigned char onion_hen_button_body[kButtonBodySize];
  unsigned char stock_app_error_body[kButtonBodySize];
};

struct HomeUiPatchOffsets {
  size_t title_id;
  size_t app_error_event_trigger;
  size_t navigate_to_home;
  size_t home_icon_order;
  size_t fps_factory;
  size_t download_error_string;
  size_t custom_icon_value;
  size_t custom_icon_uri;
  size_t top_nav_link_uri;
  size_t custom_title_value;
  size_t fps_body;
  /* ApplicationErrorEventTrigger function start (77-byte button host). */
  size_t app_error_body;
};

struct HomeUiPatchProfile {
  const char *name;
  uint32_t hbc_version;
  size_t file_length;
  unsigned char source_hash[kHbcSourceHashSize];
  HomeUiPatchOffsets offsets;
  HomeUiPatchBytes bytes;
};

/* Firmware-independent patch bytes (identical across every profile). */
static const unsigned char kLegacyTopNavPressUri[] = {
    'O', 'n', 'i', 'o', 'n', 'H', 'E', 'N', '?', 'N', 'a', 'v', 'U', 'I'};
static const unsigned char kStockDownloadErrorString[] = {
    'd', 'o', 'w', 'n', 'l', 'o', 'a', 'd', '_', 'e', 'r', 'r', 'o', 'r'};
static const unsigned char kOldCustomIconUri[] = {
    'h', 'o', 'm', 'e', 'u', 'i', ' ', 'A', 'p', 'p', 'l',
    'i', 'c', 'a', 't', 'i', 'o', 'n', 'E', 'r', 'r', 'o',
    'r', 'E', 'v', 'e', 'n', 't', ' ', 't', 'e', 's', 't'};
static const unsigned char kNewCustomIconUri[] = {
    '/', 's', 'y', 's', 't', 'e', 'm', '_', 'e', 'x', '/',
    'v', 's', 'h', '_', 'a', 's', 's', 'e', 't', '/', 'o',
    'n', 'i', 'o', 'n', 'h', 'e', 'n', '.', 'p', 'n', 'g'};
static const unsigned char kOldTopNavLinkUri[] = {
    'T', 'r', 'i', 'g', 'g', 'e', 'r', ' ',
    'A', 'p', 'p', 'E', 'r', 'r', 'o', 'r'};
static const unsigned char kLegacyBlankTopNavLinkUri[] = {
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
static const unsigned char kLegacyPaddedTopNavLinkUri[] = {
    'O', 'n', 'i', 'o', 'n', 'H', 'E', 'N',
    '?', 'N', 'a', 'v', 'U', 'I', ' ', ' '};
static const unsigned char kNewTopNavLinkUri[] = {
    'O', 'n', 'i', 'o', 'n', 'H', 'E', 'N',
    '?', 'N', 'a', 'v', 'U', 'I', '=', '1'};
static const unsigned char kNewCustomTitleValue[] = {0xff, 0x00};

#include "homeui_top_nav_profiles.inc"

static bool range_contains(size_t size, size_t offset, size_t len) {
  return offset <= size && len <= size - offset;
}

static bool bytes_equal(const unsigned char *lhs, const unsigned char *rhs,
                        size_t len) {
  return memcmp(lhs, rhs, len) == 0;
}

static uint32_t read_u32le(const unsigned char *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool has_magic(const unsigned char *data, size_t size,
                      const unsigned char *magic, size_t magic_size) {
  return range_contains(size, 0, magic_size) &&
         bytes_equal(data, magic, magic_size);
}

static bool hbc_at(unsigned char *buffer, size_t available, size_t offset,
                   HbcView *out) {
  if (!range_contains(available, offset, sizeof(kHermesMagic)) ||
      !has_magic(buffer + offset, available - offset, kHermesMagic,
                 sizeof(kHermesMagic))) {
    return false;
  }

  out->data = buffer + offset;
  out->size = available - offset;
  out->base_offset = offset;
  return true;
}

static bool locate_hbc(unsigned char *buffer, size_t visible_size,
                       size_t capacity, HbcView *out) {
  const size_t available = capacity > visible_size ? capacity : visible_size;

  if (hbc_at(buffer, available, 0, out)) {
    return true;
  }

  if (!has_magic(buffer, available, kRnpsMagic, sizeof(kRnpsMagic))) {
    return false;
  }

  size_t rnps_payload_offset = kRnpsFallbackPayloadOffset;
  if (range_contains(available, kRnpsPayloadOffsetField, sizeof(uint32_t))) {
    const uint32_t declared_offset =
        read_u32le(buffer + kRnpsPayloadOffsetField);
    if (declared_offset > 0 && declared_offset < available) {
      rnps_payload_offset = declared_offset;
    }
  }

  if (hbc_at(buffer, available, rnps_payload_offset, out)) {
    return true;
  }

  if (rnps_payload_offset != kRnpsFallbackPayloadOffset &&
      hbc_at(buffer, available, kRnpsFallbackPayloadOffset, out)) {
    return true;
  }

  return false;
}

static bool read_hbc_file_length(const HbcView &hbc, size_t *file_length) {
  if (!range_contains(hbc.size, kHbcFileLengthOffset, sizeof(uint32_t))) {
    return false;
  }

  const uint32_t declared_file_length =
      read_u32le(hbc.data + kHbcFileLengthOffset);
  if (declared_file_length <= kHbcFooterSha1Size ||
      declared_file_length > hbc.size) {
    return false;
  }

  *file_length = declared_file_length;
  return true;
}

static bool read_hbc_version(const HbcView &hbc, uint32_t *version) {
  if (!range_contains(hbc.size, kHbcVersionOffset, sizeof(uint32_t))) {
    return false;
  }

  *version = read_u32le(hbc.data + kHbcVersionOffset);
  return true;
}

static bool hbc_bytes_at(const HbcView &hbc, size_t offset,
                         const char *expected) {
  const size_t len = strlen(expected);
  return range_contains(hbc.size, offset, len) &&
         bytes_equal(hbc.data + offset,
                     reinterpret_cast<const unsigned char *>(expected), len);
}

static bool hbc_source_hash_matches(const HbcView &hbc,
                                    const unsigned char *source_hash) {
  return range_contains(hbc.size, kHbcSourceHashOffset, kHbcSourceHashSize) &&
         bytes_equal(hbc.data + kHbcSourceHashOffset, source_hash,
                     kHbcSourceHashSize);
}

static void format_hbc_source_hash(const HbcView &hbc, char *out,
                                   size_t out_size) {
  static const char kHex[] = "0123456789abcdef";
  if (!out || out_size == 0) {
    return;
  }

  out[0] = '\0';
  if (out_size < kHbcSourceHashSize * 2 + 1 ||
      !range_contains(hbc.size, kHbcSourceHashOffset, kHbcSourceHashSize)) {
    return;
  }

  const unsigned char *hash = hbc.data + kHbcSourceHashOffset;
  for (size_t i = 0; i < kHbcSourceHashSize; ++i) {
    out[i * 2] = kHex[hash[i] >> 4];
    out[i * 2 + 1] = kHex[hash[i] & 0x0f];
  }
  out[kHbcSourceHashSize * 2] = '\0';
}

static bool validate_homeui_profile_markers(const HbcView &hbc,
                                            const HomeUiPatchProfile &profile) {
  return hbc_bytes_at(hbc, profile.offsets.title_id, "NPXS40002") &&
         hbc_bytes_at(hbc, profile.offsets.app_error_event_trigger,
                      "ApplicationErrorEventTrigger") &&
         hbc_bytes_at(hbc, profile.offsets.navigate_to_home,
                      "pshomeui:navigateToHome");
}

static const HomeUiPatchProfile *
find_homeui_patch_profile(const HbcView &hbc, size_t file_length,
                          uint32_t hbc_version) {
  for (size_t i = 0;
       i < sizeof(kHomeUiPatchProfiles) / sizeof(kHomeUiPatchProfiles[0]);
       ++i) {
    const HomeUiPatchProfile &profile = kHomeUiPatchProfiles[i];
    if (file_length == profile.file_length &&
        hbc_version == profile.hbc_version &&
        hbc_source_hash_matches(hbc, profile.source_hash) &&
        validate_homeui_profile_markers(hbc, profile)) {
      return &profile;
    }
  }

  return nullptr;
}

static bool looks_like_profiled_homeui(const HbcView &hbc) {
  for (size_t i = 0;
       i < sizeof(kHomeUiPatchProfiles) / sizeof(kHomeUiPatchProfiles[0]);
       ++i) {
    const HomeUiPatchProfile &profile = kHomeUiPatchProfiles[i];
    if (validate_homeui_profile_markers(hbc, profile)) {
      return true;
    }
  }

  return false;
}

static bool validate_patch(const HbcView &hbc, const BytePatch &patch,
                           bool *already_applied) {
  if (!range_contains(hbc.size, patch.offset, patch.size)) {
#if SHELL_DEBUG == 1
    shellui_log("homeui_top_nav_patch: %s out of range at hbc+0x%llx",
                patch.name, (unsigned long long)patch.offset);
#endif
    return false;
  }

  const unsigned char *current = hbc.data + patch.offset;
  if (bytes_equal(current, patch.replacement, patch.size)) {
    *already_applied = true;
    return true;
  }

  if (bytes_equal(current, patch.expected, patch.size) ||
      (patch.alternate_expected &&
       bytes_equal(current, patch.alternate_expected, patch.size)) ||
      (patch.second_alternate_expected &&
       bytes_equal(current, patch.second_alternate_expected, patch.size))) {
    *already_applied = false;
    return true;
  }

#if SHELL_DEBUG == 1
  shellui_log("homeui_top_nav_patch: %s mismatch at hbc+0x%llx; skip",
              patch.name, (unsigned long long)patch.offset);
#endif
  return false;
}

static void apply_patch(const HbcView &hbc, const BytePatch &patch) {
  const unsigned char *current = hbc.data + patch.offset;
  if (!bytes_equal(current, patch.replacement, patch.size)) {
    memcpy(hbc.data + patch.offset, patch.replacement, patch.size);
  }
}

static void update_hbc_footer_sha1(HbcView &hbc, size_t file_length) {
  const size_t footer_offset = file_length - kHbcFooterSha1Size;
  SHA1_CTX ctx;

  SHA1Init(&ctx);
  SHA1Update(&ctx, hbc.data, (uint32_t)footer_offset);
  SHA1Final(hbc.data + footer_offset, &ctx);
}

/*
 * Focused top-nav buttons use invertedIconSource. Named system icons (Search,
 * Settings) resolve both states from iconId; a custom file URI only fills the
 * normal source unless we mirror it into invertedIcon.
 *
 * Crash history was from Fps body hijack on game-close remount, not from this
 * mirror itself. Still harden: path filter, exception-safe ToString, re-entry
 * guard (SetinvertedIconSource must not re-enter SetIconSource).
 */
static bool is_homeui_top_nav_icon_source(MonoObject *source) {
  if (!source || !mono_object_to_string) {
    return false;
  }

  MonoObject *exception = nullptr;
  MonoString *text = mono_object_to_string(source, &exception);
  if (exception || !text) {
    return false;
  }

  return Mono_to_String(text).find(kOnionHenTopNavIconPath) !=
         std::string::npos;
}

static void ReactButtonShadowNode_SetIconSource_Hook(MonoObject *instance,
                                                     MonoObject *source) {
  thread_local int depth = 0;

  if (g_react_button_set_icon_source_orig) {
    g_react_button_set_icon_source_orig(instance, source);
  }

  if (depth > 0 || !shellui_hooks_are_ready() || !instance || !source ||
      !g_react_button_set_inverted_icon_source ||
      !is_homeui_top_nav_icon_source(source)) {
    return;
  }

  ++depth;
  g_react_button_set_inverted_icon_source(instance, source);
  --depth;
#if SHELL_DEBUG == 1
  shellui_log("homeui_top_nav_patch: mirrored OnionHEN icon to invertedIcon");
#endif
}

} // namespace

#endif /* SHELLUI_HOMEUI_TOP_NAV_PATCH == 1 */

void install_homeui_top_nav_hooks(MonoImage *react_pui) {
#if SHELLUI_HOMEUI_TOP_NAV_PATCH == 1
  if (!react_pui) {
    shellui_log("homeui_top_nav_patch: ReactNative.PUI image missing");
    return;
  }

  g_react_button_set_inverted_icon_source =
      reinterpret_cast<void (*)(MonoObject *, MonoObject *)>(
          Get_Address_of_Method(react_pui, "ReactNative.Views.UI3.View",
                                "ReactButtonShadowNode",
                                "SetinvertedIconSource", 1));
  if (!g_react_button_set_inverted_icon_source) {
    shellui_log("homeui_top_nav_patch: SetinvertedIconSource missing; "
                "focused icon may be blank");
  }

  const uint64_t set_icon_source =
      Get_Address_of_Method(react_pui, "ReactNative.Views.UI3.View",
                            "ReactButtonShadowNode", "SetIconSource", 1);
  if (!set_icon_source) {
    shellui_log("homeui_top_nav_patch: SetIconSource missing");
    return;
  }

  const bool installed = InstallDetour(
      set_icon_source,
      reinterpret_cast<void *>(&ReactButtonShadowNode_SetIconSource_Hook),
      reinterpret_cast<void **>(&g_react_button_set_icon_source_orig));
  shellui_log(installed ? "homeui_top_nav_patch: SetIconSource hooked "
                          "(invertedIcon mirror for OnionHEN)"
                        : "homeui_top_nav_patch: SetIconSource detour failed");
#else
  (void)react_pui;
#endif
}

void patch_homeui_top_nav(unsigned char *buffer, int *size_ptr,
                          int buffer_capacity) {
#if SHELLUI_HOMEUI_TOP_NAV_PATCH == 1
  if (!buffer || !size_ptr || *size_ptr <= 0) {
    return;
  }

  HbcView hbc = {};
  const size_t visible_size = (size_t)*size_ptr;
  const size_t capacity =
      buffer_capacity > *size_ptr ? (size_t)buffer_capacity : visible_size;
  if (!locate_hbc(buffer, visible_size, capacity, &hbc)) {
#if SHELL_DEBUG == 1
    const unsigned char b0 = visible_size > 0 ? buffer[0] : 0;
    const unsigned char b1 = visible_size > 1 ? buffer[1] : 0;
    const unsigned char b2 = visible_size > 2 ? buffer[2] : 0;
    const unsigned char b3 = visible_size > 3 ? buffer[3] : 0;
    shellui_log("homeui_top_nav_patch: no HBC candidate (size=%d capacity=%d "
                "head=%02x %02x %02x %02x)",
                *size_ptr, buffer_capacity, b0, b1, b2, b3);
#endif
    return;
  }

  size_t hbc_file_length = 0;
  if (!read_hbc_file_length(hbc, &hbc_file_length)) {
#if SHELL_DEBUG == 1
    shellui_log("homeui_top_nav_patch: invalid HBC length at base+0x%llx",
                (unsigned long long)hbc.base_offset);
#endif
    return;
  }

  uint32_t hbc_version = 0;
  if (!read_hbc_version(hbc, &hbc_version)) {
#if SHELL_DEBUG == 1
    shellui_log("homeui_top_nav_patch: invalid HBC version at base+0x%llx",
                (unsigned long long)hbc.base_offset);
#endif
    return;
  }

  const HomeUiPatchProfile *profile =
      find_homeui_patch_profile(hbc, hbc_file_length, hbc_version);
  if (!profile) {
#if SHELL_DEBUG == 1
    if (looks_like_profiled_homeui(hbc)) {
      if (!g_logged_unsupported_homeui_skip.exchange(
              true, std::memory_order_relaxed)) {
        char source_hash[kHbcSourceHashSize * 2 + 1];
        format_hbc_source_hash(hbc, source_hash, sizeof(source_hash));
        shellui_log("homeui_top_nav_patch: unsupported HomeUI HBC "
                    "(hbc_base=0x%llx version=%u file_length=0x%llx "
                    "source_hash=%s)",
                    (unsigned long long)hbc.base_offset, hbc_version,
                    (unsigned long long)hbc_file_length, source_hash);
      }
    } else if (!g_logged_non_homeui_skip.exchange(true,
                                                   std::memory_order_relaxed)) {
      shellui_log("homeui_top_nav_patch: skip non-HomeUI HBC "
                  "(hbc_base=0x%llx version=%u file_length=0x%llx)",
                  (unsigned long long)hbc.base_offset, hbc_version,
                  (unsigned long long)hbc_file_length);
    }
#endif
    return;
  }

#if SHELL_DEBUG == 1
  shellui_log("homeui_top_nav_patch: matched profile '%s' hbc_base=0x%llx "
              "version=%u hbc_file_length=0x%llx size=%d capacity=%d",
              profile->name,
              (unsigned long long)hbc.base_offset,
              hbc_version, (unsigned long long)hbc_file_length, *size_ptr,
              buffer_capacity);
#endif

  /*
   * Root cause of ShellUI SIGSEGV on game close (SceRnJs-rnps-home):
   *
   * Earlier builds rewrote the Fps *function body* (131-byte showFps debug
   * component) into a useInteractivePress button and put Fps in the top-nav
   * array. Fps remounts on BIG_APP → home focus restore and crashed the RN JS
   * executor.
   *
   * Safe design (keeps the toolbox top-nav entry):
   *
   *   top-nav order: [Search, ApplicationErrorEventTrigger, Settings, Profile]
   *   host function: ApplicationErrorEventTrigger (already a 77-byte button)
   *   body:          full 77-byte useInteractivePress OnionHEN button
   *   Fps:           restore/leave stock showFps implementation
   *   focus icon:    SetIconSource hook mirrors onionhen.png → invertedIcon
   *
   * Object table still retargets:
   *   iconId string → /system_ex/vsh_asset/onionhen.png
   *   title id      → empty
   *   Trigger AppError string slot → OnionHEN?NavUI=1 (hook_boot → toolbox)
   *
   * Accept prior in-memory shapes (Fps-in-array + Fps body rewrite, factory
   * alias) and repair them toward this layout.
   */

  const HomeUiPatchBytes &bytes = profile->bytes;
  const unsigned char *legacy_button_body =
      bytes.has_legacy_button_body ? bytes.legacy_onion_hen_button_body
                                   : nullptr;

  const BytePatch kPatches[] = {
      /*
       * Order: stock / prior Fps-slot strategy / aliased → AppError slot.
       * replacement is ApplicationErrorEventTrigger between Search & Settings.
       */
      {"home icon order", profile->offsets.home_icon_order,
       bytes.old_icon_order, bytes.legacy_fps_slot_icon_order,
       bytes.legacy_aliased_icon_order, bytes.app_error_icon_order,
       kIconOrderSize},
      {"legacy Fps factory repair", profile->offsets.fps_factory,
       bytes.legacy_aliased_fps_factory, nullptr, nullptr,
       bytes.original_fps_factory, kFpsFactorySize},
      {"download_error string repair", profile->offsets.download_error_string,
       kLegacyTopNavPressUri, nullptr, nullptr, kStockDownloadErrorString,
       sizeof(kStockDownloadErrorString)},
      {"custom icon value", profile->offsets.custom_icon_value,
       bytes.old_custom_icon_value, nullptr, nullptr,
       bytes.new_custom_icon_value, kObjectTableValueSize},
      {"custom icon uri", profile->offsets.custom_icon_uri, kOldCustomIconUri,
       nullptr, nullptr, kNewCustomIconUri, sizeof(kOldCustomIconUri)},
      {"top-nav link uri", profile->offsets.top_nav_link_uri,
       kOldTopNavLinkUri, kLegacyBlankTopNavLinkUri,
       kLegacyPaddedTopNavLinkUri,
       kNewTopNavLinkUri, sizeof(kOldTopNavLinkUri)},
      {"custom title value", profile->offsets.custom_title_value,
       bytes.old_custom_title_value, nullptr, nullptr, kNewCustomTitleValue,
       kObjectTableValueSize},
      /*
       * Repair prior Fps-body hijack back to stock showFps (prefix only).
       * expected = onion body still sitting on Fps; replacement = stock Fps.
       */
      {"Fps body repair", profile->offsets.fps_body,
       bytes.onion_hen_button_body, legacy_button_body,
       nullptr, bytes.old_fps_body_prefix, kButtonBodySize},
      /*
       * Host OnionHEN on ApplicationErrorEventTrigger (exact 77-byte replace).
       */
      {"AppError OnionHEN body", profile->offsets.app_error_body,
       bytes.stock_app_error_body, bytes.onion_hen_button_body,
       legacy_button_body, bytes.onion_hen_button_body,
       kButtonBodySize},
  };

  bool any_change = false;
  for (size_t i = 0; i < sizeof(kPatches) / sizeof(kPatches[0]); ++i) {
    bool already_applied = false;
    if (!validate_patch(hbc, kPatches[i], &already_applied)) {
      return;
    }
    any_change = any_change || !already_applied;
  }

  if (!any_change) {
#if SHELL_DEBUG == 1
    shellui_log("homeui_top_nav_patch: already applied profile='%s' "
                "(hbc_base=0x%llx)",
                profile->name, (unsigned long long)hbc.base_offset);
#endif
    return;
  }

  for (size_t i = 0; i < sizeof(kPatches) / sizeof(kPatches[0]); ++i) {
    apply_patch(hbc, kPatches[i]);
  }
  update_hbc_footer_sha1(hbc, hbc_file_length);

#if SHELL_DEBUG == 1
  shellui_log("homeui_top_nav_patch: activated OnionHEN top-nav slot "
              "profile='%s' "
              "(hbc_base=0x%llx)",
              profile->name, (unsigned long long)hbc.base_offset);
#endif
#else
  (void)buffer;
  (void)size_ptr;
  (void)buffer_capacity;
#endif
}

void shellui_request_homeui_top_nav_reload(void) {
#if SHELLUI_HOMEUI_TOP_NAV_PATCH == 1
  g_homeui_top_nav_reload_pending.store(true, std::memory_order_release);
  shellui_log("homeui_top_nav_patch: queued NPXS40002 reload for next UI tick");
#endif
}

void shellui_poll_homeui_top_nav_reload(void) {
#if SHELLUI_HOMEUI_TOP_NAV_PATCH == 1
  if (!g_homeui_top_nav_reload_pending.exchange(false,
                                                std::memory_order_acq_rel)) {
    return;
  }

  shellui_log("homeui_top_nav_patch: applying NPXS40002 reload on UI thread");
  ReloadRNPSApp("NPXS40002");
#endif
}
