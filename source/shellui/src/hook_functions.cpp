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

#include "hooked_funcs.hpp"
#include "homeui_top_nav_patch.hpp"
#include <onion/debug_settings_route_policy.hpp>
#include <onion/platform.h>
#include "remote_play.h"
#include "detour.h"
#include "ipc.hpp"
#include <climits>
#include <msg.hpp>
#include <pthread.h>
#include <sys/_pthreadtypes.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <fstream>
#include <unistd.h>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdint.h>
#include <sha1.hpp>
extern "C"{
#include <ps5/kernel.h>
}

extern bool is_6xx, is_3xx;
/* ================================= ORIG HOOKED MONO FUNCS ============================================= */
int (*oOnPress)(MonoObject* Instance, MonoObject* element, MonoObject* e) = nullptr;
int (*oOnPreCreate)(MonoObject* Instance, MonoObject* element) = nullptr;
MonoString* (*CxmlUri)(MonoObject* obj, MonoString* uri) = nullptr;
uint64_t(*GetManifestResourceStream_Original)(uint64_t inst, MonoString* FileName) = nullptr;
uint64_t(*GetManifestResourceInternal_Orig)(MonoObject* instance, MonoString* name, int* size, MonoObject& module) = nullptr;
void (*DebugSettings_GetModel_Orig)(MonoObject* instance, MonoObject* param, MonoObject* promise) = nullptr;
void (*ReactNavigatorManager_UpdateNavigationState_Orig)(MonoObject* instance, MonoObject* state) = nullptr;
void (*UpdateImposeStatusFlag_Orig)(MonoObject* Instance, MonoObject* a) = nullptr;
bool (*CheckRemotePlayRestriction_Orig)(MonoObject* instance) = nullptr;
void (*oTerminate)(void) = nullptr;
GamePadData (*GetData)(int deviceIndex) = nullptr;

bool (*boot_orig)(MonoString* uri, int opt, MonoString* titleIdForBootAction) = nullptr;
void (*OnShareButton_orig)(MonoObject* data) = nullptr;
bool (*boot_orig_2)(MonoString* uri, int opt) = nullptr;

void (*CaptureScreen_orig_old)(MonoObject * inst, int userId, long deviceId, int capType, MonoObject* capacityInfo) = nullptr;
void (*CaptureScreen_orig_new)(MonoObject * inst, int userId, long deviceId, int capType, MonoString* format, MonoObject* capacityInfo) = nullptr;
void (*createJson)(MonoObject*, MonoObject* array, MonoString* id, MonoString* label, MonoString* actionUrl, MonoString* actionId, MonoString* messageId, MonoObject* subMenu, bool enable) = nullptr;

int (*__sys_regmgr_call)(long, long, int*, int*, long) = nullptr;

MonoString *(*oGetString)(MonoObject *Instance, MonoString *str) = nullptr;
int (*LaunchApp_orig)(MonoString* titleId, uint64_t* args, int argsSize, LaunchAppParam *param) = nullptr;

// Store original function pointer
DecryptRnpsBundle_t DecryptRnpsBundle = NULL;



/* ================================= HOOKED GLOBAL VARS ============================================= */
MonoClass* MemoryStream_IO = nullptr;

// UI runtime state: g_ui (shellui_state.hpp / shellui_globals.cpp)

#define MAX_CHEATS 256


// widgets → shellui_overlay_widgets.cpp
extern "C"{
int sceShellCoreUtilIsUsbMassStorageMounted(int num);
int sceNetSend(int sockfd, const void *buf, size_t len, int flags);
}

/** Create MonoString on the *calling* domain (UI thread may not be Root_Domain). */
static MonoString *mono_str_ui(const char *utf8) {
  if (!utf8 || !mono_string_new)
    return nullptr;
  MonoDomain *dom =
      (mono_domain_get ? mono_domain_get() : nullptr);
  if (!dom)
    dom = Root_Domain;
  if (!dom)
    return nullptr;
  return mono_string_new(dom, utf8);
}

MonoString *GetString_Hook(MonoObject *Instance, MonoString *str) {
    if (!shellui_hooks_are_ready())
      return oGetString ? oGetString(Instance, str) : str;

    if (!str || !Instance) {
#if SHELL_DEBUG == 1
      LOG_ERROR("GetString_Hook: Invalid Parameters");
#endif
      /* Prefer original; never invent a string on a broken call. */
      if (oGetString)
        return oGetString(Instance, str);
      return str;
    }
    std::string resourceName = Mono_to_String(str);
#if SHELL_DEBUG == 1
    LOG_DEBUG("Resource Name: %s", resourceName.c_str());
#endif
    if (resourceName == "msg_options") {
      return mono_str_ui("PKG 安装器选项");
    } else if (resourceName == "msg_installing") {
      return mono_str_ui("OnionHEN 正在安装所选 PKG");
    } else if (resourceName == "msg_yes") {
      return mono_str_ui("是");
    } else if (resourceName == "msg_no") {
      return mono_str_ui("否");
    } else if (resourceName == "msg_sort") {
      return mono_str_ui("OnionHEN PKG 排序");
    } else if (resourceName == "msg_sort_name_az") {
      return mono_str_ui("名称（A-Z）");
    } else if (resourceName == "msg_sort_name_za") {
      return mono_str_ui("名称（Z-A）");
    } else if (resourceName == "msg_updated") {
      return mono_str_ui("已更新");
    } else if (resourceName == "msg_wait") {
      return mono_str_ui("请稍候...");
    }
    else if (resourceName == "msg_ok"){
      return mono_str_ui("确定");
    }
    else if (resourceName == "msg_cancel_vb"){
        return mono_str_ui("取消");
    }
    //else if (resourceName == "msg_deselect_all") {
   //   return mono_str_ui("取消全选"); // IDK WHY BUT ONLY 1 CAN BE ACTIVE OR SHELLUI CRASHES
  //  }
    else if (resourceName == "msg_select_all") {
      return mono_str_ui("全选");
    }

    // XML title/description literals (e.g. "★OnionHEN 工具箱") are already valid
    // MonoStrings. Re-allocating with mono_string_new(Root_Domain, ...) on the UI
    // thread has crashed ShellUI (wrong domain / GC). Pass the original through.
    if (resourceName.rfind("msg_", 0) != 0) {
#if SHELL_DEBUG == 1
      LOG_DEBUG("GetString_Hook: literal XML string, passthrough");
#endif
      return str;
    }

    if (!oGetString) {
#if SHELL_DEBUG == 1
      LOG_DEBUG("GetString_Hook: oGetString is null");
#endif
      return str;
    }
    return oGetString(Instance, str);
  }
  


/*
 * Hermes stores non-ASCII strings as UTF-16LE in the decrypted RNPS/HBC
 * buffer (see kylin-core develop-pro-shell-ui shellui_overlay.c). Searching
 * for UTF-8 "★Debug Settings" never matches, so the Settings list kept
 * showing Debug Settings. Replace equal-length UTF-16LE sequences in place.
 */
static int patch_utf16le_once(unsigned char *buf, int size,
                              const unsigned char *old_bytes, size_t old_len,
                              const unsigned char *new_bytes, size_t new_len) {
  if (!buf || size <= 0 || old_len == 0 || old_len != new_len)
    return 0;
  for (int i = 0; i + (int)old_len <= size; i++) {
    if (memcmp(buf + i, old_bytes, old_len) == 0) {
      memcpy(buf + i, new_bytes, new_len);
      return 1;
    }
  }
  return 0;
}

static bool patch_buffer_contains(const unsigned char *buf, size_t size,
                                  const void *needle, size_t needle_len) {
  if (!buf || !needle || needle_len == 0 || needle_len > size) {
    return false;
  }

  const unsigned char *needle_bytes =
      reinterpret_cast<const unsigned char *>(needle);
  for (size_t i = 0; i + needle_len <= size; ++i) {
    if (memcmp(buf + i, needle_bytes, needle_len) == 0) {
      return true;
    }
  }
  return false;
}

static uint32_t patch_read_u32le(const unsigned char *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool locate_hermes_payload(const unsigned char *buffer, size_t size,
                                  const unsigned char **hbc,
                                  size_t *hbc_size) {
  static const unsigned char kHermesMagic[] = {0xc6, 0x1f, 0xbc, 0x03,
                                               0xc1, 0x03, 0x19, 0x1f};

  const size_t kMaxHeaderScan = 0x2000;
  const size_t scan_size = size < kMaxHeaderScan ? size : kMaxHeaderScan;
  for (size_t i = 0; i + sizeof(kHermesMagic) <= scan_size; ++i) {
    if (memcmp(buffer + i, kHermesMagic, sizeof(kHermesMagic)) == 0) {
      *hbc = buffer + i;
      *hbc_size = size - i;
      return true;
    }
  }
  return false;
}

static bool update_hermes_footer_sha1(unsigned char *buffer, size_t size) {
  static constexpr size_t kHbcFileLengthOffset = 0x20;
  static constexpr size_t kHbcFooterSha1Size = 20;

  const unsigned char *located_hbc = nullptr;
  size_t hbc_size = 0;
  if (!locate_hermes_payload(buffer, size, &located_hbc, &hbc_size) ||
      hbc_size < kHbcFileLengthOffset + sizeof(uint32_t)) {
    return false;
  }

  const uint32_t file_length =
      patch_read_u32le(located_hbc + kHbcFileLengthOffset);
  if (file_length < kHbcFooterSha1Size || file_length > hbc_size) {
    return false;
  }

  unsigned char *hbc = buffer + (located_hbc - buffer);
  const size_t footer_offset = file_length - kHbcFooterSha1Size;
  SHA1_CTX ctx;
  SHA1Init(&ctx);
  SHA1Update(&ctx, hbc, static_cast<uint32_t>(footer_offset));
  SHA1Final(hbc + footer_offset, &ctx);
  return true;
}

/* 4.x, 6.x, 7.x and 8.x NPXS40008 use pre-Hermes RNPS JavaScript bundles. */
static constexpr unsigned char kLegacySettingsBundleMagic[] = {
    0xe5, 0xd1, 0x0b, 0xfb};
static constexpr size_t kLegacySettingsPayloadOffsetFallback = 0xb20;
static constexpr size_t kLegacySettingsPayloadOffset = 0xb30;

struct LegacySettingsBundleProfile {
  const char *name;
  size_t payload_size;
  size_t label_offset;
  size_t icon_offset;
};

static constexpr LegacySettingsBundleProfile kLegacySettingsProfiles[] = {
    {"4.03 NPXS40008 Settings", 0x483280, 0x234a17, 0x24db26},
    /* 4.50 and 4.51 NPXS40008 are byte-identical. */
    {"4.50/4.51 NPXS40008 Settings", 0x483fc0, 0x234f2d, 0x24e03c},
    /* 6.00 and 6.02 NPXS40008 are byte-identical. */
    {"6.00/6.02 NPXS40008 Settings", 0x5524a0, 0x27f5dc, 0x299152},
    /* 7.40 and 7.61 NPXS40008 are byte-identical. */
    {"7.40/7.61 NPXS40008 Settings", 0x5e9d20, 0x2bac8c, 0x2d56e1},
    {"8.00 NPXS40008 Settings", 0x64bb80, 0x2e75c9, 0x302a6f},
    {"8.40 NPXS40008 Settings", 0x654af0, 0x2e62fd, 0x3017a3},
};

static constexpr unsigned char kLegacySettingsOldLabel[] = {
    0xe2, 0x98, 0x85, 'D', 'e', 'b', 'u', 'g', ' ', 'S', 'e', 't', 't', 'i',
    'n', 'g', 's'};
static constexpr unsigned char kLegacySettingsNewLabel[] = {
    0xe2, 0x98, 0x85, 'O', 'n', 'i', 'o', 'n', 'H', 'E', 'N', ' ', 'T', 'o',
    'o', 'l', 's'};
static constexpr unsigned char kLegacySettingsOldIcon[] = {
    'i', 'c', 'o', 'n', '_', 's', 'e', 't', 't', 'i', 'n', 'g'};
static constexpr unsigned char kLegacySettingsNewIcon[] = {
    'o', 'n', 'i', 'o', 'n', 'h', '_', 's', 'i', 'c', 'o', 'n'};

static_assert(sizeof(kLegacySettingsOldLabel) ==
              sizeof(kLegacySettingsNewLabel));
static_assert(sizeof(kLegacySettingsOldIcon) ==
              sizeof(kLegacySettingsNewIcon));

static bool settings_bytes_at(const unsigned char *buffer, size_t size,
                              size_t offset, const unsigned char *expected,
                              size_t expected_size) {
  return buffer && offset <= size && expected_size <= size - offset &&
         memcmp(buffer + offset, expected, expected_size) == 0;
}

static const LegacySettingsBundleProfile *
locate_legacy_settings_profile(const unsigned char *buffer, size_t size,
                               const unsigned char **payload,
                               size_t *payload_size) {
  if (!buffer || size < sizeof(kLegacySettingsBundleMagic)) {
    return nullptr;
  }

  size_t offset = 0;
  if (memcmp(buffer, kLegacySettingsBundleMagic,
             sizeof(kLegacySettingsBundleMagic)) != 0) {
    if (size < 0x20 ||
        memcmp(buffer, "RNPSHEDR", sizeof("RNPSHEDR") - 1) != 0) {
      return nullptr;
    }

    offset = patch_read_u32le(buffer + 0x1c);
    if (offset == 0 || offset >= size) {
      offset = kLegacySettingsPayloadOffsetFallback;
    }
    if (offset + sizeof(kLegacySettingsBundleMagic) > size ||
        memcmp(buffer + offset, kLegacySettingsBundleMagic,
               sizeof(kLegacySettingsBundleMagic)) != 0) {
      if (offset == kLegacySettingsPayloadOffsetFallback ||
          kLegacySettingsPayloadOffset + sizeof(kLegacySettingsBundleMagic) >
              size ||
          memcmp(buffer + kLegacySettingsPayloadOffset,
                 kLegacySettingsBundleMagic,
                 sizeof(kLegacySettingsBundleMagic)) != 0) {
        return nullptr;
      }
      offset = kLegacySettingsPayloadOffset;
    }
  }

  const size_t available = size - offset;
  for (const LegacySettingsBundleProfile &profile : kLegacySettingsProfiles) {
    if (available != profile.payload_size) {
      continue;
    }
    const unsigned char *data = buffer + offset;
    const bool label_known =
        settings_bytes_at(data, available, profile.label_offset,
                          kLegacySettingsOldLabel,
                          sizeof(kLegacySettingsOldLabel)) ||
        settings_bytes_at(data, available, profile.label_offset,
                          kLegacySettingsNewLabel,
                          sizeof(kLegacySettingsNewLabel));
    const bool icon_known =
        settings_bytes_at(data, available, profile.icon_offset,
                          kLegacySettingsOldIcon,
                          sizeof(kLegacySettingsOldIcon)) ||
        settings_bytes_at(data, available, profile.icon_offset,
                          kLegacySettingsNewIcon,
                          sizeof(kLegacySettingsNewIcon));
    if (label_known && icon_known) {
      *payload = data;
      *payload_size = available;
      return &profile;
    }
  }
  return nullptr;
}

static int patch_settings_bytes_once(unsigned char *buffer, size_t size,
                                     size_t offset,
                                     const unsigned char *old_bytes,
                                     const unsigned char *new_bytes,
                                     size_t byte_count) {
  if (!settings_bytes_at(buffer, size, offset, old_bytes, byte_count)) {
    return 0;
  }
  memcpy(buffer + offset, new_bytes, byte_count);
  return 1;
}

static bool patch_legacy_settings_bundle(unsigned char *buffer, size_t size,
                                         int *label_count, int *icon_count) {
  const unsigned char *payload = nullptr;
  size_t payload_size = 0;
  const LegacySettingsBundleProfile *profile =
      locate_legacy_settings_profile(buffer, size, &payload, &payload_size);
  if (!profile) {
    return false;
  }

  unsigned char *writable_payload = buffer + (payload - buffer);
  *label_count = patch_settings_bytes_once(
      writable_payload, payload_size, profile->label_offset,
      kLegacySettingsOldLabel, kLegacySettingsNewLabel,
      sizeof(kLegacySettingsOldLabel));
  *icon_count = patch_settings_bytes_once(
      writable_payload, payload_size, profile->icon_offset,
      kLegacySettingsOldIcon, kLegacySettingsNewIcon,
      sizeof(kLegacySettingsOldIcon));
  return true;
}

static bool is_supported_settings_bundle(const unsigned char *buffer,
                                         int size) {
  static const size_t kHbcFileLengthOffset = 0x20;
  static const size_t kHbcSourceHashOffset = 0x0c;
  static const size_t kHbcFooterSha1Size = 20;
  static const char kSettingsUriPrefix[] = "pssettings:play";
  static const char kDebugSettingsFunction[] = "debug_settings";

  const unsigned char *hbc = nullptr;
  size_t hbc_size = 0;
  if (!buffer || size <= 0 ||
      !locate_hermes_payload(buffer, (size_t)size, &hbc, &hbc_size) ||
      hbc_size < kHbcFileLengthOffset + sizeof(uint32_t) ||
      hbc_size < kHbcSourceHashOffset +
                     onion::debug_settings_route::kSourceHashLength) {
    return false;
  }

  const uint32_t hbc_file_length = patch_read_u32le(hbc + kHbcFileLengthOffset);
  if (hbc_file_length < kHbcFooterSha1Size || hbc_file_length > hbc_size) {
    return false;
  }
  const uint8_t *source_hash = hbc + kHbcSourceHashOffset;
  if (!onion::debug_settings_route::settings_bundle_is_supported(
          hbc_file_length, source_hash)) {
    return false;
  }

  return patch_buffer_contains(hbc, hbc_size, kSettingsUriPrefix,
                               sizeof(kSettingsUriPrefix) - 1) &&
         patch_buffer_contains(hbc, hbc_size, kDebugSettingsFunction,
                               sizeof(kDebugSettingsFunction) - 1);
}

void patch_bundle_strings(unsigned char* buffer, int* size_ptr, int buffer_capacity) {
  if (!buffer || !size_ptr || *size_ptr <= 0) {
      return;
  }
  const int size = *size_ptr;

  /* ★Debug Settings → ★OnionHEN Tools  (15 UTF-16 code units = 30 bytes) */
  static const unsigned char kOldDbgLabel[] = {
      0x05, 0x26, /* ★ U+2605 */
      'D', 0x00, 'e', 0x00, 'b', 0x00, 'u', 0x00, 'g', 0x00,
      ' ', 0x00, 'S', 0x00, 'e', 0x00, 't', 0x00, 't', 0x00,
      'i', 0x00, 'n', 0x00, 'g', 0x00, 's', 0x00,
  };
  static const unsigned char kNewDbgLabel[] = {
      0x05, 0x26, /* ★ */
      'O', 0x00, 'n', 0x00, 'i', 0x00, 'o', 0x00, 'n', 0x00,
      'H', 0x00, 'E', 0x00, 'N', 0x00, ' ', 0x00, 'T', 0x00,
      'o', 0x00, 'o', 0x00, 'l', 0x00, 's', 0x00,
  };
  static_assert(sizeof(kOldDbgLabel) == sizeof(kNewDbgLabel),
                "UTF-16 Debug Settings label must be equal length");

  int legacy_label_count = 0;
  int legacy_icon_count = 0;
  const bool is_legacy_settings = patch_legacy_settings_bundle(
      buffer, (size_t)size, &legacy_label_count, &legacy_icon_count);
  if (is_legacy_settings) {
#if SHELL_DEBUG == 1
    LOG_DEBUG("patch_bundle_strings: legacy NPXS40008 settings patch "
              "label=%d icon=%d",
              legacy_label_count, legacy_icon_count);
#endif
  } else if (is_supported_settings_bundle(buffer, size)) {
    int label_count = patch_utf16le_once(
        buffer, size, kOldDbgLabel, sizeof(kOldDbgLabel), kNewDbgLabel,
        sizeof(kNewDbgLabel));
    const bool footer_updated =
        label_count == 0 || update_hermes_footer_sha1(buffer, (size_t)size);
#if SHELL_DEBUG == 1
    if (!footer_updated) {
      LOG_ERROR("patch_bundle_strings: NPXS40008 footer SHA-1 update failed");
    }
    LOG_DEBUG("patch_bundle_strings: NPXS40008 settings patch label=%d "
              "footer=%s (stock icon id preserved)",
              label_count, footer_updated ? "ok" : "failed");
#else
    (void)label_count;
    (void)footer_updated;
#endif
  }

  patch_homeui_top_nav(buffer, size_ptr, buffer_capacity);
}

int ioctl_hook(int fd, unsigned long request, void *argp) {
  const int IOCTL_SYSCALL = 0x36;
  const unsigned long  DECRYPT_RNPS_BUNDLE = 0xC0105203; // RNPS request code for ioctl

  int ret = __syscall(IOCTL_SYSCALL, fd, request, argp);
  if (shellui_hooks_are_ready() && ret == 0 && request == DECRYPT_RNPS_BUNDLE) {
      ioctl_C0105203_args *args = (ioctl_C0105203_args *)argp;
#if SHELL_DEBUG == 1
      LOG_DEBUG("ioctl_hook called with fd: %d, request: 0x%lX, argp: %p", fd, request, argp);
#endif
      if (!args || !args->buffer || args->size <= 0) {
#if SHELL_DEBUG == 1
          LOG_ERROR("homeui_top_nav_patch: ioctl RNPS args invalid");
#endif
          return ret;
      }
#if SHELL_DEBUG == 1
      const unsigned char *p = (const unsigned char *)args->buffer;
      const unsigned char b0 = args->size > 0 ? p[0] : 0;
      const unsigned char b1 = args->size > 1 ? p[1] : 0;
      const unsigned char b2 = args->size > 2 ? p[2] : 0;
      const unsigned char b3 = args->size > 3 ? p[3] : 0;
      LOG_DEBUG("homeui_top_nav_patch: ioctl RNPS buffer=%p size=%d "
                  "head=%02x %02x %02x %02x",
                  args->buffer, args->size, b0, b1, b2, b3);
#endif
      patch_bundle_strings((unsigned char*)args->buffer, &args->size, args->size);
  }
  return ret;
}

void ParseCheatID(const char* id, char* tid, int* cheat_id)
{
    sscanf(id, "id_cheat_%[^_]_%d", tid, cheat_id);
}

//
// Scene has changed: end Remote Play PIN registration if we are leaving that page.
// Installed via LayerManager.UpdateImposeStatusFlag (optional / non-required).
//
void UpdateImposeStatusFlag_hook(MonoObject* scene, MonoObject* frontActiveScene)
{
    LOG_DEBUG("[DBG-UIS] enter scene=%p front=%p orig=%p remote=%d confirm=%d",
                static_cast<void *>(scene), static_cast<void *>(frontActiveScene),
                reinterpret_cast<void*>(UpdateImposeStatusFlag_Orig),
                g_ui.is_active_page(toolbox::Page::RemotePlay) ? 1 : 0,
                IsRunningConfirmRegistLoop ? 1 : 0);
    if(!frontActiveScene || !scene) {
        LOG_WARN("[DBG-UIS] scene or frontActiveScene null — skip RP cleanup");
        if (UpdateImposeStatusFlag_Orig)
            UpdateImposeStatusFlag_Orig(scene, frontActiveScene);
        return;
    }

    /*
     * Old logic only stopped the loop when is_remote_play was already false,
     * so a real leave (flag still true) never called StopConfirmRegistLoop.
     * On any scene transition away from the RP page, end registration fully.
     */
    if (g_ui.is_active_page(toolbox::Page::RemotePlay) ||
        IsRunningConfirmRegistLoop) {
        LOG_DEBUG("[DBG-UIS] scene change — end remote play registration "
                    "(remote=%d confirm=%d)",
                    g_ui.is_active_page(toolbox::Page::RemotePlay) ? 1 : 0,
                    IsRunningConfirmRegistLoop ? 1 : 0);
        StopConfirmRegistLoop();
        g_ui.leave_page(toolbox::Page::RemotePlay);
    }

    if (UpdateImposeStatusFlag_Orig)
        UpdateImposeStatusFlag_Orig(scene, frontActiveScene);
    LOG_DEBUG("[DBG-UIS] original returned");
}


// threads → hook_background.cpp
MonoString * CxmlUri_Hook(MonoObject * Instance, MonoString * uri) {

  if (!shellui_hooks_are_ready())
    return CxmlUri ? CxmlUri(Instance, uri) : uri;

  if (!Instance || !uri) {
    #if SHELL_DEBUG==1 
    LOG_DEBUG("CxmlUri_Hook: args are null");
    #endif
    return CxmlUri(Instance, uri);
  }
  std::string uri_string = Mono_to_String(uri);
  #if SHELL_DEBUG==1 
  LOG_DEBUG("uri_string: %s", uri_string.c_str());
  #endif
  ///LOG_DEBUG("CxmlUri_Hook: %s", uri_string.c_str());
  if (uri_string.rfind("tex_game_icon") != std::string::npos) {
    //LOG_DEBUG("CxmlUri_Hook: Returning store icon");
    std::string icon = "/user/appmeta/" + g_ui.running_tid + "/icon0.png";
    if(!if_exists(icon.c_str())){
        icon = "/user/appmeta/external/" + g_ui.running_tid + "/icon0.png";

        if(!if_exists(icon.c_str())){ // pirated PS5 Games
           std::string game_src = "/system_ex/app/" + g_ui.running_tid + "/sce_sys/icon0.png"; // shellui cant access this path
           icon = "/user/appmeta/" + g_ui.running_tid;
           mkdir(icon.c_str(), 0777);
           icon = "/user/appmeta/" + g_ui.running_tid + "/icon0.png";
           IPC_Client::getInstance(false).CopyFile(game_src, icon);
        }
    }
   // LOG_DEBUG("CxmlUri_Hook: %s", icon.c_str());
    return mono_str_ui(icon.c_str());
  }
  else if (uri_string.rfind("//usb") != std::string::npos || uri_string.rfind("//data") != std::string::npos || uri_string.rfind("//user//data") != std::string::npos){
    //replace // with//
    std::string new_uri = uri_string;
    size_t pos = 0;
    while (( pos = new_uri.find("//", pos)) != std::string::npos) {
        new_uri.replace(pos, 2, "/");
    }
    #if SHELL_DEBUG==1 
    LOG_DEBUG("CxmlUri_Hook: %s", new_uri.c_str());
    #endif
    return mono_str_ui(new_uri.c_str());
  }
  return CxmlUri(Instance, uri);
}
MonoObject* MemoryStream_Instance = nullptr;


// navigator → hook_navigator.cpp
// manifest → hook_manifest.cpp
extern "C" int sceKernelGetPs4SystemSwVersion(OrbisKernelSwVersion *);

MonoMethod* set_value_method = nullptr;
void CheckRunningOnMainThread() {
	//notify("Main thread check called!");
}
void Patch_Main_thread_Check(MonoImage * image_core) {

    uint64_t real_addr = Get_Address_of_Method(image_core, "Sce.PlayStation.Core.Runtime", "Diagnostics", "CheckRunningOnMainThread", 0);
    if (!real_addr) {
#if SHELL_DEBUG==1
        LOG_ERROR("Failed to get method address");
#endif
        return;
    }
#if SHELL_DEBUG==1
    LOG_DEBUG("changing permissions on (0x%llx).", (unsigned long long)real_addr);
#endif
    
    if (!DetourFunction(real_addr, (void*)&CheckRunningOnMainThread)) {
        LOG_ERROR("Main thread check detour failed");
        return;
    }
#if SHELL_DEBUG==1
    LOG_DEBUG("Main thread check patched");
#endif

}
// Common logic function

// launch/terminate → hook_launch.cpp
