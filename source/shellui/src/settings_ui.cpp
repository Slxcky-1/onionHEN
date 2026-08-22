/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Extracted from mono_utils.cpp for module locality.
 */

#include "hooked_funcs.hpp"
#include "toolbox_i18n.hpp"
#include <onion/platform.h>
#include <onion/ready.h>
#include <onion/log_settings.hpp>
#include "ipc.hpp" // shellui_log + IPC_Client
#include <onion/settings.hpp>

#include <cmath>

namespace {

bool valid_dimension(float value) {
  return std::isfinite(value) && value > 1.0f;
}

} // namespace

void apply_overlay_layout() {
  /*
   * Edge strip overlay:
   *   Panel X=0, Width = RootWidget full width,
   *   Y = 0 (top edge) or screenH - barH (bottom edge),
   *   Height = font_size + 6.
   * Metrics pack L→R and are centered *inside* the full-width strip.
   */
  const float kScreenW = g_overlay_layout.screen_w;
  const float kScreenH = g_overlay_layout.screen_h;
  /* font_size=18, panel height = font + 6. */
  constexpr float kFontH = 18.0f;
  constexpr float kBarExtra = 6.0f;
  /* Labels: PositionType=1 + MarginTop=5 + FitHeightToText=true. */
  constexpr float kTextTopInset = 5.0f;
  constexpr float w_cpu_avg = 190.0f;
  constexpr float w_cpu_all = 420.0f;
  constexpr float w_gpu = 190.0f;
  constexpr float w_ram = 170.0f;
  constexpr float w_ip = 200.0f;
  constexpr float w_fps = 130.0f;
  constexpr float kGap = 28.0f; /* roomy group gap (not packed) */
  constexpr float kOffscreen = -4096.0f;

  if (!valid_dimension(kScreenW) || !valid_dimension(kScreenH)) {
    g_overlay_layout.bar_x = 0.0f;
    g_overlay_layout.bar_y = 0.0f;
    g_overlay_layout.bar_w = 0.0f;
    g_overlay_layout.bar_h = kFontH + kBarExtra;
    g_overlay_layout.label_margin_top = kTextTopInset;
    g_overlay_layout.overlay_cpu_x = kOffscreen;
    g_overlay_layout.overlay_cpu_y = 0.0f;
    g_overlay_layout.overlay_gpu_x = kOffscreen;
    g_overlay_layout.overlay_gpu_y = 0.0f;
    g_overlay_layout.overlay_ram_x = kOffscreen;
    g_overlay_layout.overlay_ram_y = 0.0f;
    g_overlay_layout.overlay_ip_x = kOffscreen;
    g_overlay_layout.overlay_ip_y = 0.0f;
    g_overlay_layout.overlay_fps_x = kOffscreen;
    g_overlay_layout.overlay_fps_y = 0.0f;
    return;
  }

  const bool show_cpu = g_settings.overlay_enabled &&
                        (g_settings.overlay_cpu || g_settings.all_cpu_usage);
  const bool show_gpu = g_settings.overlay_enabled && g_settings.overlay_gpu;
  const bool show_ram = g_settings.overlay_enabled && g_settings.overlay_ram;
  const bool show_ip = g_settings.overlay_enabled && g_settings.overlay_ip;
  const bool show_fps = g_settings.overlay_enabled && g_settings.overlay_fps;
  const float w_cpu = g_settings.all_cpu_usage ? w_cpu_all : w_cpu_avg;

  float content_w = 0.0f;
  int n = 0;
  auto acc = [&](bool on, float w) {
    if (!on)
      return;
    if (n++)
      content_w += kGap;
    content_w += w;
  };
  acc(show_fps, w_fps);
  acc(show_cpu, w_cpu);
  acc(show_gpu, w_gpu);
  acc(show_ram, w_ram);
  acc(show_ip, w_ip);

  const bool bottom =
      (g_settings.overlay_pos == OVERLAY_POS_BOTTOM_LEFT ||
       g_settings.overlay_pos == OVERLAY_POS_BOTTOM_RIGHT);

  const float bar_h = kFontH + kBarExtra;
  /* Full-bleed edge strip: X=0, Width=root width. */
  const float bar_x = 0.0f;
  const float bar_w = kScreenW;
  const float bar_y = bottom ? (kScreenH - bar_h) : 0.0f;
  const float label_margin_top = kTextTopInset;
  float x = content_w > 0.0f ? ((kScreenW - content_w) * 0.5f) : kOffscreen;

  g_overlay_layout.bar_x = bar_x;
  g_overlay_layout.bar_y = bar_y;
  g_overlay_layout.bar_w = bar_w;
  g_overlay_layout.bar_h = bar_h;
  g_overlay_layout.label_margin_top = label_margin_top;

  auto place = [&](float &ox, float &oy, bool on, float w) {
    oy = bar_y;
    if (!on) {
      ox = kOffscreen;
      return;
    }
    ox = x;
    x += w + kGap;
  };

  place(g_overlay_layout.overlay_fps_x, g_overlay_layout.overlay_fps_y,
        show_fps, w_fps);
  place(g_overlay_layout.overlay_cpu_x, g_overlay_layout.overlay_cpu_y,
        show_cpu, w_cpu);
  place(g_overlay_layout.overlay_gpu_x, g_overlay_layout.overlay_gpu_y,
        show_gpu, w_gpu);
  place(g_overlay_layout.overlay_ram_x, g_overlay_layout.overlay_ram_y,
        show_ram, w_ram);
  place(g_overlay_layout.overlay_ip_x, g_overlay_layout.overlay_ip_y, show_ip,
        w_ip);
}

void apply_overlay_layout(float screen_w, float screen_h) {
  const bool dimensions_ready =
      valid_dimension(screen_w) && valid_dimension(screen_h);
  g_overlay_layout.screen_w = dimensions_ready ? screen_w : 0.0f;
  g_overlay_layout.screen_h = dimensions_ready ? screen_h : 0.0f;
  apply_overlay_layout();
}

bool LoadSettings()
{
  onion::Settings s{};
  /* false from settings_load means defaults only — not a hard failure. */
  const bool from_file = onion::settings_load(&s);
  const onion_log_level effective = onion::apply_log_settings(s);
  if (!from_file) {
    LOG_ERROR("config.ini missing; using defaults");
  } else {
    LOG_DEBUG("Loaded settings from %s", onion::settings_last_loaded_path());
  }
  if (effective != static_cast<onion_log_level>(s.log_level)) {
    LOG_WARN("ShellUI log level '%s' unavailable in this build; using '%s'",
             onion_log_level_name(static_cast<onion_log_level>(s.log_level)),
             onion_log_level_name(effective));
  }

  // Process-local store (UI thread); twin disk paths via settings_load/save.
  g_settings = s;
  toolbox_i18n::apply_system_or_ui_lang(s.ui_lang);
  /* Clear stale fps_overlay marker left by older shellui builds. */
  onion_ready_clear(ONION_FLAG_FPS_OVERLAY);
  apply_overlay_layout();
  /* Always true once defaults-or-file applied (prx boot requires success). */
  return true;
}

bool SaveSettings()
{
  if (!onion::settings_save(g_settings)) {
    LOG_ERROR("Failed to save settings to any config path");
    return false;
  }
  LOG_DEBUG("Saved settings (primary + shellui paths when writable)");
  return true;
}

void settings_commit(bool reload_main, bool reload_util)
{
  if (!SaveSettings()) {
    return;
  }
  if (reload_main) {
    IPC_Client::getInstance(false).Reload_Daemon_Settings();
  }
  if (reload_util) {
    IPC_Client::getInstance(true).Reload_Daemon_Settings();
  }
  /* Apply last so failures while persisting/propagating remain visible even
     when the newly selected level is off or more restrictive. */
  (void)onion::apply_log_settings(g_settings);
}
