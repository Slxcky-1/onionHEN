/* Copyright (C) 2025 OnionHEN / LightningMods - OnPress built-in plugins */

#include "onpress.hpp"

/*
 * The Plugins page lists each built-in plugin as a <link> that the stock
 * settings UI navigates natively (file="<plugin>.xml"). Each plugin's config
 * page then binds its controls to the shared handlers below, so start/stop and
 * scan behavior stay in one place.
 */
static const OnPressExactEntry kPluginsExact[] = {
    {"id_plugin_kstuff_autoload", onpress_kstuff_autoload},
    {"id_plugin_delete_kstuff", onpress_delete_kstuff},
    {"id_plugin_ftpsrv_run", onpress_ftp_autoload},
    {"id_plugin_shadowmount_autoload", onpress_shadowmount_autoload},
    {"id_plugin_shadowmount_scan", onpress_shadowmount_scan},
    {"id_plugin_shadowmount_remove_external",
     onpress_shadowmount_remove_external},
};

const OnPressExactEntry *onpress_plugins_exact(size_t *count) {
  *count = sizeof(kPluginsExact) / sizeof(kPluginsExact[0]);
  return kPluginsExact;
}
