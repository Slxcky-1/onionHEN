/* Copyright (C) 2025 OnionHEN / LightningMods - OnPress built-in plugins */

#include "onpress.hpp"
#include "shellui_payload_state.hpp"

namespace {

OnPressResult toggle_plugin_now(OnPressContext &ctx, const char *key,
                                DaemonCommands cmd, const char *fail_notify,
                                const char *on_notify, const char *off_notify) {
  ctx.dirty = false;
  const bool enabled = value_as_int(ctx);
  if (enabled == shellui_payload_is_running(key))
    return OnPressResult::EarlyReturn;

  if (IPC_Client::getInstance(true).ToggleSetting(cmd, enabled) !=
      IPC_Ret::NO_ERROR) {
    notify(fail_notify);
    return OnPressResult::EarlyReturn;
  }
  notify(enabled ? on_notify : off_notify);
  return OnPressResult::Handled;
}

OnPressResult toggle_next_boot(OnPressContext &ctx, bool &field,
                               const char *on_notify, const char *off_notify) {
  const bool enabled = value_as_int(ctx);
  if (enabled == field)
    return OnPressResult::EarlyReturn;
  field = enabled;
  notify(enabled ? on_notify : off_notify);
  return OnPressResult::Handled;
}

} // namespace

OnPressResult onpress_ftp_run(OnPressContext &ctx) {
  return toggle_plugin_now(ctx, "ftpsrv", BREW_UTIL_TOGGLE_FTP,
                           "notify.ftp.toggle_failed", "notify.ftp.enabled",
                           "notify.ftp.disabled");
}

OnPressResult onpress_ftp_autoload(OnPressContext &ctx) {
  return toggle_next_boot(ctx, g_settings.ftp_autoload,
                          "notify.ftp.next_boot_on", "notify.ftp.next_boot_off");
}

OnPressResult onpress_shadowmount_run(OnPressContext &ctx) {
  return toggle_plugin_now(ctx, "shadowmountplus", BREW_UTIL_TOGGLE_SHADOWMOUNT,
                           "notify.shadowmount.toggle_failed",
                           "notify.shadowmount.enabled",
                           "notify.shadowmount.disabled");
}

OnPressResult onpress_shadowmount_autoload(OnPressContext &ctx) {
  return toggle_next_boot(ctx, g_settings.shadowmount_autoload,
                          "notify.shadowmount.next_boot_on",
                          "notify.shadowmount.next_boot_off");
}

/*
 * The Plugins page lists each built-in plugin as a <link> that the stock
 * settings UI navigates natively (file="<plugin>.xml"). Each plugin's config
 * page then binds its controls to the shared handlers below, so start/stop and
 * scan behavior stay in one place.
 */
static const OnPressExactEntry kPluginsExact[] = {
    {"id_plugin_kstuff_autoload", onpress_kstuff_autoload},
    {"id_plugin_delete_kstuff", onpress_delete_kstuff},
    {"id_plugin_ftpsrv_run", onpress_ftp_run},
    {"id_plugin_ftpsrv_autoload", onpress_ftp_autoload},
    {"id_plugin_shadowmount_run", onpress_shadowmount_run},
    {"id_plugin_shadowmount_autoload", onpress_shadowmount_autoload},
};

const OnPressExactEntry *onpress_plugins_exact(size_t *count) {
  *count = sizeof(kPluginsExact) / sizeof(kPluginsExact[0]);
  return kPluginsExact;
}
