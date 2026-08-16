/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Remote Play page lifecycle coordinator.
 */
#pragma once

struct MonoObject;
struct MonoImage;

namespace toolbox {
enum class Page : unsigned char;
}

/** Resolve optional Legacy/PUI capabilities used by the lifecycle poller. */
void InitializeRemotePlayPageLifecycle(MonoImage *legacy, MonoImage *pui);

/** Record the toolbox page that owns the Remote Play link before XML loads. */
void BeginRemotePlayPageLoad(toolbox::Page previous_page);

/** Attach the actual Legacy SettingPage that owns the pairing controls. */
void AttachRemotePlayPage(MonoObject *page);

/** True only for the retained Legacy SettingPage instance. */
bool IsRemotePlayPage(MonoObject *page);

/** Mark the retained page active from SettingPage.OnActivated. */
void ActivateRemotePlayPage(MonoObject *page);

enum class RemotePlayPageBackReason : unsigned char {
  PairingSucceeded = 1,
  PairingTimedOut,
};

/** Pairing worker event; the actual page pop is performed on the UI thread. */
void RequestRemotePlayPageBack(RemotePlayPageBackReason reason);

/** Observe Settings visibility and consume deferred navigation once per frame. */
void PollRemotePlayPageLifecycle(MonoObject *application);

enum class RemotePlayExitDestination : unsigned char {
  OutsideToolbox = 0,
  PreviousToolboxPage,
};

/** End registration and detach page-owned state. Idempotent. */
void EndRemotePlayPageSession(
    const char *reason,
    RemotePlayExitDestination destination =
        RemotePlayExitDestination::OutsideToolbox);
