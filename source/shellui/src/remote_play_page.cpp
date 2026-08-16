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
std::atomic_bool g_timeout_back_requested{false};

MonoObject *(*g_get_page_plugin)(MonoObject *) = nullptr;
MonoObject *(*g_get_plugin_ui_manager)(MonoObject *) = nullptr;
MonoObject *(*g_get_page_stack)(MonoObject *) = nullptr;
MonoObject *(*g_get_current_page)(MonoObject *) = nullptr;
bool (*g_get_manager_active)(MonoObject *) = nullptr;
bool (*g_get_actual_visible)(MonoObject *) = nullptr;
void (*g_pop_page)(MonoObject *, int) = nullptr;

void release_page() {
  if (g_remote_play_page_handle != 0 && mono_gchandle_free)
    mono_gchandle_free(g_remote_play_page_handle);
  g_remote_play_page = nullptr;
  g_remote_play_page_handle = 0;
}

} // namespace

bool InitializeRemotePlayPageLifecycle(MonoImage *legacy, MonoImage *pui) {
  if (!legacy || !pui)
    return false;

  g_get_page_plugin = reinterpret_cast<MonoObject *(*)(MonoObject *)>(
      Get_Address_of_Method(legacy, UI3_dec.c_str(), "SettingPage",
                            "get_Plugin", 0));
  g_get_plugin_ui_manager = reinterpret_cast<MonoObject *(*)(MonoObject *)>(
      Get_Address_of_Method(legacy, UI3_dec.c_str(), "SettingsPlugin",
                            "get_UIManager", 0));
  g_get_page_stack = reinterpret_cast<MonoObject *(*)(MonoObject *)>(
      Get_Address_of_Method(legacy, UI3_dec.c_str(), "UIManager",
                            "get_PageStack", 0));
  g_get_current_page = reinterpret_cast<MonoObject *(*)(MonoObject *)>(
      Get_Address_of_Method(legacy, UI3_dec.c_str(), "SettingPageStack",
                            "get_Current", 0));
  g_get_manager_active = reinterpret_cast<bool (*)(MonoObject *)>(
      Get_Address_of_Method(legacy, UI3_dec.c_str(), "UIManager",
                            "get_Active", 0));
  g_get_actual_visible = reinterpret_cast<bool (*)(MonoObject *)>(
      Get_Address_of_Method(pui, "Sce.PlayStation.PUI", "SceneBase",
                            "get_ActualVisible", 0));
  g_pop_page = reinterpret_cast<void (*)(MonoObject *, int)>(
      Get_Address_of_Method(legacy, UI3_dec.c_str(), "UIManager", "Pop", 1));

  const bool ready = g_get_page_plugin && g_get_plugin_ui_manager &&
                     g_get_page_stack && g_get_current_page &&
                     g_get_manager_active && g_get_actual_visible &&
                     g_pop_page;
  LOG_DEBUG("[remote_play] page lifecycle methods %s",
            ready ? "ready" : "incomplete");
  return ready;
}

void BeginRemotePlayPageLoad(toolbox::Page previous_page) {
  if (previous_page != toolbox::Page::RemotePlay)
    g_previous_page = previous_page;
  release_page();
  g_timeout_back_requested.store(false, std::memory_order_release);
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
  LOG_DEBUG("[remote_play] attached SettingPage=%p", static_cast<void *>(page));
}

bool IsRemotePlayPage(MonoObject *page) {
  return page && page == g_remote_play_page;
}

void RequestRemotePlayTimeoutBack() {
  g_timeout_back_requested.store(true, std::memory_order_release);
}

void PollRemotePlayPageLifecycle() {
  if (g_page_phase == remote_play::PagePhase::Inactive ||
      !g_remote_play_page || !g_get_page_plugin ||
      !g_get_plugin_ui_manager || !g_get_page_stack || !g_get_current_page ||
      !g_get_manager_active || !g_get_actual_visible || !g_pop_page) {
    return;
  }

  MonoObject *plugin = g_get_page_plugin(g_remote_play_page);
  MonoObject *manager = plugin ? g_get_plugin_ui_manager(plugin) : nullptr;
  const bool manager_active = manager && g_get_manager_active(manager);
  MonoObject *stack = manager ? g_get_page_stack(manager) : nullptr;
  MonoObject *current_page = stack ? g_get_current_page(stack) : nullptr;
  const bool actual_visible = g_get_actual_visible(g_remote_play_page);
  const remote_play::PageObservation observation{
      manager_active, current_page == g_remote_play_page, actual_visible};

  switch (remote_play::classify_page_observation(g_page_phase, observation)) {
  case remote_play::PageObservationAction::Wait:
  case remote_play::PageObservationAction::StayVisible:
    break;
  case remote_play::PageObservationAction::MarkVisible:
    g_page_phase = remote_play::PagePhase::Visible;
    LOG_DEBUG("[remote_play] page is active and visible");
    break;
  case remote_play::PageObservationAction::LeaveToPrevious:
    EndRemotePlayPageSession(
        "page_stack_changed",
        RemotePlayExitDestination::PreviousToolboxPage);
    return;
  case remote_play::PageObservationAction::LeaveToolbox:
    EndRemotePlayPageSession("settings_not_visible");
    return;
  }

  if (g_page_phase == remote_play::PagePhase::Visible &&
      g_timeout_back_requested.exchange(false, std::memory_order_acq_rel)) {
    g_page_phase = remote_play::PagePhase::BackRequested;
    LOG_DEBUG("[remote_play] timeout: popping page on UI thread");
    g_pop_page(manager, 0); // TransitionAnimationType.Default
  }
}

void EndRemotePlayPageSession(const char *reason,
                              RemotePlayExitDestination destination) {
  if (g_page_phase == remote_play::PagePhase::Inactive)
    return;

  LOG_DEBUG("[remote_play] end page session (%s)",
            reason ? reason : "unknown");
  g_page_phase = remote_play::PagePhase::Inactive;
  g_timeout_back_requested.store(false, std::memory_order_release);
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
