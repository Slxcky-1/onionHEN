/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Page-scoped lifecycle hooks for dynamic Legacy settings pages.
 */

#include "hooked_funcs.hpp"

#include "remote_play_page.hpp"

void OnActivated_Hook(MonoObject *instance, int transition) {
  const bool is_remote_play = IsRemotePlayPage(instance);
  if (oOnActivated)
    oOnActivated(instance, transition);
  if (shellui_hooks_are_ready() && is_remote_play)
    ActivateRemotePlayPage(instance);
}

void OnDeactivating_Hook(MonoObject *instance, int transition) {
  const bool is_remote_play = IsRemotePlayPage(instance);
  if (oOnDeactivating)
    oOnDeactivating(instance, transition);
  if (shellui_hooks_are_ready() && is_remote_play)
    EndRemotePlayPageSession(
        "page_deactivating",
        transition == 2 ? RemotePlayExitDestination::PreviousToolboxPage
                        : RemotePlayExitDestination::OutsideToolbox);
}
