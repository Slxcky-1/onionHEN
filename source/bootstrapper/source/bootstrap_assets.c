/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_assets.h"

#include <onion/fs.h>
#include <onion/log.h>
#include <onion/notify.h>

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

extern uint8_t sicon_start[];
extern const unsigned int sicon_size;
extern uint8_t ftpsrv_plugin_start[];
extern const unsigned int ftpsrv_plugin_size;

#define FTPSRV_PLUGIN_PATH "/data/OnionHEN/plugins/FTPS00001.elf"
#define FTPSRV_PLUGIN_AUTO "/data/OnionHEN/plugins/FTPS00001.elf.auto_start"
#define FTPSRV_PLUGIN_MARK "/data/OnionHEN/plugins/FTPS00001.bundled"
#define FTPSRV_PLUGIN_MIN 65536u

static void bootstrap_assets_install_ftp(void) {
  if (ftpsrv_plugin_size < FTPSRV_PLUGIN_MIN)
    return;
  mkdir("/data/OnionHEN", 0777);
  mkdir("/data/OnionHEN/plugins", 0777);
  const bool first = !if_exists(FTPSRV_PLUGIN_MARK);
  if (!write_file_atomic(FTPSRV_PLUGIN_PATH, ftpsrv_plugin_start,
                         ftpsrv_plugin_size)) {
    LOG_WARN("bundled FTP plugin write failed");
    return;
  }
  (void)touch_file(FTPSRV_PLUGIN_MARK);
  if (first)
    (void)touch_file(FTPSRV_PLUGIN_AUTO);
}

#define ONION_ICON(name)                                                       \
  extern uint8_t name##_start[];                                               \
  extern const unsigned int name##_size;
#include "icon_manifest.inc"
#undef ONION_ICON

typedef struct EmbeddedIcon {
  const char *name;
  const uint8_t *data;
  const unsigned int *size;
} EmbeddedIcon;

static const EmbeddedIcon kEmbeddedIcons[] = {
#define ONION_ICON(name) {#name, name##_start, &name##_size},
#include "icon_manifest.inc"
#undef ONION_ICON
};

bool bootstrap_assets_write(void) {
  mkdir("/data/OnionHEN", 0777);
  mkdir("/data/OnionHEN/assets", 0777);
  mkdir("/user/data", 0777);
  mkdir("/user/data/OnionHEN", 0777);

  const bool startup_icon_ready = write_file_atomic(
      ONION_NOTIFY_ICON_PATH, sicon_start, sicon_size);

  for (size_t i = 0; i < sizeof(kEmbeddedIcons) / sizeof(kEmbeddedIcons[0]);
       ++i) {
    char path[256];
    snprintf(path, sizeof(path), "/data/OnionHEN/assets/%s.png",
             kEmbeddedIcons[i].name);
    (void)write_file_atomic(path, kEmbeddedIcons[i].data,
                            *kEmbeddedIcons[i].size);
  }

  mkdir("/system_ex/vsh_asset", 0777);
  (void)write_file_atomic("/system_ex/vsh_asset/onionhen.png", sicon_start,
                          sicon_size);
  bootstrap_assets_install_ftp();
  return startup_icon_ready;
}
