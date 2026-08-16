/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "remote_play_page.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "remote_play.h"
#include "remote_play_page_lifecycle.hpp"
#include "shellui_state.hpp"

#include <atomic>

namespace {

MonoObject *g_remote_play_page = nullptr;
uint32_t g_remote_play_page_handle = 0;
toolbox::Page g_previous_page = toolbox::Page::None;
remote_play::PagePhase g_page_phase = remote_play::PagePhase::Inactive;
std::atomic<unsigned char> g_page_back_reason{0};

MonoObject *(*g_get_page_plugin)(MonoObject *) = nullptr;
MonoObject *(*g_get_plugin_ui_manager)(MonoObject *) = nullptr;
MonoObject *(*g_get_page_stack)(MonoObject *) = nullptr;
MonoObject *(*g_get_current_page)(MonoObject *) = nullptr;
MonoObject *(*g_get_focus_active_scene)(MonoObject *) = nullptr;
void (*g_pop_page)(MonoObject *, int) = nullptr;

void release_page() {
  if (g_remote_play_page_handle != 0 && mono_gchandle_free)
    mono_gchandle_free(g_remote_play_page_handle);
  g_remote_play_page = nullptr;
  g_remote_play_page_handle = 0;
}

} // namespace

void InitializeRemotePlayPageLifecycle(MonoImage *legacy, MonoImage *pui) {
  g_get_page_plugin = legacy
                          ? reinterpret_cast<MonoObject *(*)(MonoObject *)>(
                                Get_Address_of_Method(
                                    legacy, UI3_dec.c_str(), "SettingPage",
                                    "get_Plugin", 0))
                          : nullptr;
  g_get_plugin_ui_manager =
      legacy ? reinterpret_cast<MonoObject *(*)(MonoObject *)>(
                   Get_Address_of_Method(legacy, UI3_dec.c_str(),
                                         "SettingsPlugin", "get_UIManager", 0))
             : nullptr;
  g_get_page_stack = legacy
                         ? reinterpret_cast<MonoObject *(*)(MonoObject *)>(
                               Get_Address_of_Method(
                                   legacy, UI3_dec.c_str(), "UIManager",
                                   "get_PageStack", 0))
                         : nullptr;
  g_get_current_page =
      legacy ? reinterpret_cast<MonoObject *(*)(MonoObject *)>(
                   Get_Address_of_Method(legacy, UI3_dec.c_str(),
                                         "SettingPageStack", "get_Current", 0))
             : nullptr;
  g_get_focus_active_scene =
      pui ? reinterpret_cast<MonoObject *(*)(MonoObject *)>(
                Get_Address_of_Method(pui, "Sce.PlayStation.PUI",
                                      "Application", "get_FocusActiveScene", 0))
          : nullptr;
  g_pop_page = legacy
                   ? reinterpret_cast<void (*)(MonoObject *, int)>(
                         Get_Address_of_Method(legacy, UI3_dec.c_str(),
                                               "UIManager", "Pop", 1))
                   : nullptr;

  const bool navigation_ready =
      g_get_page_plugin && g_get_plugin_ui_manager && g_pop_page;
  const bool focus_ready = g_get_focus_active_scene != nullptr;
  const bool stack_ready = g_get_page_stack && g_get_current_page;
  LOG_INFO("[remote_play] lifecycle capabilities focus_scene=%d page_stack=%d "
           "ui_pop=%d",
           focus_ready ? 1 : 0, stack_ready ? 1 : 0,
           navigation_ready ? 1 : 0);
  if (!focus_ready)
    LOG_WARN("[remote_play] PS-button foreground-exit detection unavailable");
  if (!navigation_ready)
    LOG_WARN("[remote_play] paired/timeout automatic page return unavailable");
  if (!stack_ready)
    LOG_WARN("[remote_play] page-stack destination detection unavailable");
}

void BeginRemotePlayPageLoad(toolbox::Page previous_page) {
  if (previous_page != toolbox::Page::RemotePlay)
    g_previous_page = previous_page;
  release_page();
  g_page_back_reason.store(0, std::memory_order_release);
  g_page_phase = remote_play::PagePhase::Loading;
}

void AttachRemotePlayPage(MonoObject *page) {
  if (!page || !mono_gchandle_new || !mono_gchandle_free)
    return;
  if (page == g_remote_play_page)
    return;

  release_page();
  const uint32_t handle = mono_gchandle_new(page, 1);
  if (handle == 0) {
    LOG_WARN("[remote_play] failed to retain SettingPage");
    return;
  }

  g_remote_play_page = page;
  g_remote_play_page_handle = handle;
  g_page_phase = remote_play::PagePhase::Loading;
  g_ui.set_active_page(toolbox::Page::RemotePlay);
  LOG_INFO("[remote_play] attached SettingPage=%p", static_cast<void *>(page));
}

bool IsRemotePlayPage(MonoObject *page) {
  return page && page == g_remote_play_page;
}

void ActivateRemotePlayPage(MonoObject *page) {
  if (!IsRemotePlayPage(page) ||
      g_page_phase == remote_play::PagePhase::Inactive)
    return;
  g_page_phase = remote_play::PagePhase::Visible;
  LOG_INFO("[remote_play] SettingPage activated");
}

void RequestRemotePlayPageBack(RemotePlayPageBackReason reason) {
  g_page_back_reason.store(static_cast<unsigned char>(reason),
                           std::memory_order_release);
}

void PollRemotePlayPageLifecycle(MonoObject *application) {
  if (g_page_phase == remote_play::PagePhase::Inactive ||
      !g_remote_play_page) {
    return;
  }

  MonoObject *manager = nullptr;
  if (g_get_page_plugin && g_get_plugin_ui_manager) {
    MonoObject *plugin = g_get_page_plugin(g_remote_play_page);
    manager = plugin ? g_get_plugin_ui_manager(plugin) : nullptr;
  }

  if (g_get_focus_active_scene && application) {
    MonoObject *focus_scene = g_get_focus_active_scene(application);
    MonoObject *stack =
        manager && g_get_page_stack ? g_get_page_stack(manager) : nullptr;
    MonoObject *current_page =
        stack && g_get_current_page ? g_get_current_page(stack) : nullptr;
    const remote_play::PageObservation observation{
        focus_scene == g_remote_play_page,
        !stack || !g_get_current_page || current_page == g_remote_play_page};

    switch (remote_play::classify_page_observation(g_page_phase,
                                                    observation)) {
    case remote_play::PageObservationAction::Wait:
    case remote_play::PageObservationAction::StayVisible:
      break;
    case remote_play::PageObservationAction::MarkVisible:
      g_page_phase = remote_play::PagePhase::Visible;
      LOG_INFO("[remote_play] page owns the active focus scene");
      break;
    case remote_play::PageObservationAction::LeaveToPrevious:
      EndRemotePlayPageSession(
          "page_stack_changed",
          RemotePlayExitDestination::PreviousToolboxPage);
      return;
    case remote_play::PageObservationAction::LeaveToolbox:
      EndRemotePlayPageSession("focus_scene_changed");
      return;
    }
  }

  const unsigned char back_reason =
      g_page_back_reason.load(std::memory_order_acquire);
  if (back_reason == 0)
    return;

  if (!g_get_page_plugin || !g_get_plugin_ui_manager || !g_pop_page) {
    g_page_back_reason.store(0, std::memory_order_release);
    LOG_WARN("[remote_play] %s: page pop capability unavailable",
             back_reason == static_cast<unsigned char>(
                                RemotePlayPageBackReason::PairingSucceeded)
                 ? "paired"
                 : "timeout");
    return;
  }
  if (!manager)
    return;

  g_page_back_reason.store(0, std::memory_order_release);
  g_page_phase = remote_play::PagePhase::BackRequested;
  LOG_INFO("[remote_play] %s: popping page on UI thread",
           back_reason == static_cast<unsigned char>(
                              RemotePlayPageBackReason::PairingSucceeded)
               ? "paired"
               : "timeout");
  g_pop_page(manager, 0); // TransitionAnimationType.Default
}

void EndRemotePlayPageSession(const char *reason,
                              RemotePlayExitDestination destination) {
  if (g_page_phase == remote_play::PagePhase::Inactive)
    return;

  LOG_INFO("[remote_play] end page session (%s)",
           reason ? reason : "unknown");
  g_page_phase = remote_play::PagePhase::Inactive;
  g_page_back_reason.store(0, std::memory_order_release);
  StopConfirmRegistLoop();
  release_page();
  if (g_ui.is_active_page(toolbox::Page::RemotePlay)) {
    g_ui.set_active_page(
        destination == RemotePlayExitDestination::PreviousToolboxPage
            ? g_previous_page
            : toolbox::Page::None);
  }
  g_previous_page = toolbox::Page::None;
}
