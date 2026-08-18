/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Cheat-download progress page backed by a Legacy Settings user_custom Panel.
 * The worker thread only polls IPC. UI3 widget creation and updates stay on the
 * ShellUI thread.
 */
#pragma once

#include "monodef.h"

#include <string>
#include <string_view>

/** Reset progress state and start the detached IPC status poller. */
void cheat_progress_show(void);

/** Generate the dynamic cheat_progress.xml document. */
void generate_cheat_progress_xml(std::string &xml_buffer);

/** Append the UI3 Panel to the Widget created for the user_custom element. */
void cheat_progress_attach_panel(std::string_view id, MonoObject *widget);

/** Apply pending status and percentage to real UI3 widgets on the UI thread. */
void shellui_poll_cheat_progress(void);
