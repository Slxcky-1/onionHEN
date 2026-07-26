/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress misc (kstuff, RP, credits) */
#include "onpress.hpp"
#include <fstream>
#include <unistd.h>

static OnPressResult id_kstuff_autoload(OnPressContext &ctx) {
  if (atol(ctx.value.c_str())) {
    unlink("/user/data/OnionHEN/no_kstuff");
    notify("Kstuff will be loaded on next boot");
  } else {
    touch_file("/user/data/OnionHEN/no_kstuff");
    notify("Kstuff will NOT be loaded on next boot");
  }
  return OnPressResult::Handled;
}

static OnPressResult id_delete_kstuff(OnPressContext &ctx) {
  (void)ctx;
  unlink("/user/data/OnionHEN/kstuff.elf");
  notify("The external kstuff has been deleted");
  return OnPressResult::Handled;
}

static OnPressResult id_save_rp_info(OnPressContext &ctx) {
  (void)ctx;
  if (usbpath() == -1) {
    notify("Failed to save Remote Play info, USB not found");
    return OnPressResult::EarlyReturn;
  }
  std::string usb_rp_path =
      "/usb" + std::to_string(usbpath()) + "/remote_play_info.txt";
  LOG_DEBUG("Saving Remote Play info to %s", usb_rp_path.c_str());
  std::ofstream rp_file(usb_rp_path);
  if (!rp_file.is_open()) {
    notify("Failed to open Remote Play info file");
    return OnPressResult::EarlyReturn;
  }
  rp_file << g_ui.remote_play_info;
  rp_file.close();
  notify("Remote Play info saved to /mnt%s", usb_rp_path.c_str());
  return OnPressResult::Handled;
}

static OnPressResult id_lm_test(OnPressContext &ctx) {
  (void)ctx;
  LOG_DEBUG("LM's Test Button Pressed");
  return OnPressResult::Handled;
}

static OnPressResult id_onionhen_credits(OnPressContext &ctx) {
  (void)ctx;
  return OnPressResult::EarlyReturn;
}

static OnPressResult id_presentation_card(OnPressContext &ctx) {
  // Author and donator cards in the dynamic Toolbox XML are display-only.
  ctx.dirty = false;
  return OnPressResult::Consumed;
}

static const OnPressExactEntry kRootExact[] = {
    {"id_kstuff_autoload", id_kstuff_autoload},
    {"id_delete_kstuff", id_delete_kstuff},
    {"id_lm_test", id_lm_test},
    {"id_onionhen_credits", id_onionhen_credits},
    {"id_author_0xp0co", id_presentation_card},
    {"id_donator_aglx", id_presentation_card},
    {"id_donator_ljf", id_presentation_card},
    {"id_donator_szx", id_presentation_card},
};

static const OnPressExactEntry kRemotePlayExact[] = {
    {"id_save_rp_info", id_save_rp_info},
};

const OnPressExactEntry *onpress_misc_root_exact(size_t *count) {
  *count = sizeof(kRootExact) / sizeof(kRootExact[0]);
  return kRootExact;
}

const OnPressExactEntry *onpress_remote_play_exact(size_t *count) {
  *count = sizeof(kRemotePlayExact) / sizeof(kRemotePlayExact[0]);
  return kRemotePlayExact;
}
