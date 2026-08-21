/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Legacy Settings' normal back-button path pops through SettingPageStack
 * directly. UIManager.Pop is only a public wrapper and does not observe that
 * path, so page-owned cleanup belongs in OnPopping(outgoing, incoming).
 */

#include "hooked_funcs.hpp"

#include "progress_dialog.hpp"
#include "shellui_state.hpp"

#include <onion/platform.h>

void SettingPageStackOnPopping_Hook(MonoObject *instance,
                                    MonoObject *outgoing,
                                    MonoObject *incoming) {
  if (shellui_hooks_are_ready() && outgoing &&
      cheat_progress_handle_popping(outgoing)) {
    g_ui.leave_page(toolbox::Page::CheatProgress);
    LOG_DEBUG("cheat_progress_xml: progress page popped and state cleared");
  }

  if (oSettingPageStackOnPopping)
    oSettingPageStackOnPopping(instance, outgoing, incoming);
}
