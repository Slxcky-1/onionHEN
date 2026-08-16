/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include "remote_play.h"
#include "ipc.hpp"
#include "remote_play_notifications.hpp"
#include "remote_play_page.hpp"
#include "remote_play_pairing.hpp"
#include <onion/account_id_b64.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <pthread.h>
#include <unistd.h>

namespace {
std::atomic_bool g_stop_requested{false};
std::atomic_bool g_session_active{false};
std::atomic<uint64_t> g_pairing_deadline_ms{0};
std::mutex g_session_command_mutex;
std::mutex g_confirm_thread_mutex;
pthread_t g_confirm_thread{};
bool g_confirm_thread_started = false;

void *confirm_regist_loop(void *);

uint64_t monotonic_milliseconds() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

/**
 * Invalidate active PIN / leave device-registration mode.
 * Required so Remote Play can accept a connection after pairing (or after the
 * user leaves the toolbox RP page). Without this, the service stays in
 * "awaiting PIN pair" and clients cannot connect immediately.
 */
void invalidate_pin_registration(const char *why) {
  if (!sceRemoteplayNotifyPinCodeError) {
    LOG_ERROR("[remote_play] NotifyPinCodeError unresolved (%s)", why);
    return;
  }
  const int err = sceRemoteplayNotifyPinCodeError(1);
  if (err != 0)
    LOG_WARN("[remote_play] NotifyPinCodeError(1) => 0x%x (%s)", err, why);
  else
    LOG_DEBUG("[remote_play] NotifyPinCodeError(1) => 0x%x (%s)", err, why);
}

void stop_confirm_thread() {
  pthread_t thread{};
  bool should_join = false;

  g_stop_requested.store(true, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(g_confirm_thread_mutex);
    should_join = g_confirm_thread_started;
    thread = g_confirm_thread;
  }

  if (should_join && !pthread_equal(pthread_self(), thread)) {
    void *result = nullptr;
    (void)pthread_join(thread, &result);
  }

  {
    std::lock_guard<std::mutex> lock(g_confirm_thread_mutex);
    if (should_join)
      g_confirm_thread_started = false;
  }
}

void stop_session() {
  g_stop_requested.store(true, std::memory_order_release);
  const bool had_session =
      g_session_active.exchange(false, std::memory_order_acq_rel);
  if (had_session)
    invalidate_pin_registration("stop_loop");
  stop_confirm_thread();
  g_pairing_deadline_ms.store(0, std::memory_order_release);
}

} // namespace

bool InitRemotePlay() {
  int rp_enable = 0, err = 0;
  if ((err = sceRegMgrGetInt_hook(REMOTE_PLAY_ENABLE_REGISTRY, &rp_enable))) {
    notify("SCE_REGMGR: unable to get REMOTEPLAY_rp_enable (0x%x)", err);
    return false;
  } else if (rp_enable != 1) {
    rp_enable = 1;
    if ((err = sceRegMgrSetInt(REMOTE_PLAY_ENABLE_REGISTRY, rp_enable))) {
      notify("SCE_REGMGR: unable to set REMOTEPLAY_rp_enable (0x%x)", err);
      return false;
    }
    int verify_enable = 0;
    if ((err = sceRegMgrGetInt_hook(REMOTE_PLAY_ENABLE_REGISTRY,
                                    &verify_enable)) ||
        verify_enable != 1) {
      notify("SCE_REGMGR: unable to verify REMOTEPLAY_rp_enable (0x%x)", err);
      return false;
    }
    LOG_DEBUG("[remote_play] enabled REMOTEPLAY_rp_enable registry");
  }

  if (!sceRemoteplayInitialize) {
    LOG_DEBUG("[remote_play] Initialize unresolved");
    return false;
  }

  // ShellUI may already have initialized this library. Keep the existing
  // session usable while recording the result for device diagnostics.
  err = sceRemoteplayInitialize(nullptr, 0);
  LOG_DEBUG("[remote_play] Initialize => 0x%x", err);
  return true;
}

bool GeneratePINCode(uint32_t& pin) {
  std::lock_guard<std::mutex> command_lock(g_session_command_mutex);
  pin = 0;

  /* Replace any previous session before creating a new registration. */
  stop_session();

  if (!sceRemoteplayGeneratePinCode) {
    LOG_DEBUG("[remote_play] GeneratePinCode unresolved");
    return false;
  }

  const int err = sceRemoteplayGeneratePinCode(&pin);
  if (err != 0) {
    LOG_ERROR("[remote_play] GeneratePinCode failed => 0x%x", err);
    return false;
  }

  const uint64_t deadline =
      monotonic_milliseconds() + remote_play::kPairingTimeoutMilliseconds;
  g_pairing_deadline_ms.store(deadline, std::memory_order_release);
  g_stop_requested.store(false, std::memory_order_release);
  g_session_active.store(true, std::memory_order_release);
  int thread_err = 0;
  {
    std::lock_guard<std::mutex> lock(g_confirm_thread_mutex);
    thread_err =
        pthread_create(&g_confirm_thread, nullptr, confirm_regist_loop, nullptr);
    g_confirm_thread_started = (thread_err == 0);
  }
  if (thread_err != 0) {
    LOG_ERROR("[remote_play] confirm thread create failed => 0x%x",
                thread_err);
    invalidate_pin_registration("thread_create_failed");
    g_session_active.store(false, std::memory_order_release);
    g_pairing_deadline_ms.store(0, std::memory_order_release);
    return false;
  }

  return true;
}

void StopConfirmRegistLoop() {
  std::lock_guard<std::mutex> command_lock(g_session_command_mutex);
  stop_session();
}

bool GetEncodedAccountID(char *buff, uint64_t &accountid,
                         bool &activated_now) {
  if (!buff) {
    return false;
  }
  buff[0] = '\0';
  accountid = 0;
  activated_now = false;

  Activator activator(true);
  if (!activator.Valid()) {
    LOG_ERROR("[remote_play] foreground account registry slot not found");
    return false;
  }

  if (activator.IsNotActivated()) {
    if (!activator.Activate()) {
      LOG_ERROR("[remote_play] account activation failed");
      return false;
    }
    activated_now = true;
  }

  accountid = activator.currentUser.accountID;
  if (accountid == 0) {
    return false;
  }
  onion_account_id_base64_encode(accountid, buff);
  return buff[0] != '\0';
}

namespace {

void *confirm_regist_loop(void *) {
  const uint64_t deadline =
      g_pairing_deadline_ms.load(std::memory_order_acquire);
  int pair_stat = -1, pair_err = -1;
  int last_pair_stat = -2, last_pair_err = -2;
  uint32_t last_notification_mark = 0;
  const char *terminal_reason = nullptr;
  bool should_return_to_previous_page = false;
  RemotePlayPageBackReason back_reason =
      RemotePlayPageBackReason::PairingTimedOut;

  LOG_DEBUG("[remote_play] ConfirmRegistLoop started");

  while (!g_stop_requested.load(std::memory_order_acquire)) {
    const uint32_t remaining_seconds = remote_play::seconds_remaining(
        deadline, monotonic_milliseconds());
    if (remaining_seconds == 0) {
      terminal_reason = "timeout";
      should_return_to_previous_page = true;
      back_reason = RemotePlayPageBackReason::PairingTimedOut;
      break;
    }

    const uint32_t notification_mark =
        remote_play::countdown_notification_mark(remaining_seconds);
    if (notification_mark != last_notification_mark) {
      NotifyRemotePlayPairingCountdown(notification_mark);
      last_notification_mark = notification_mark;
    }

    if (!sceRemoteplayConfirmDeviceRegist) {
      LOG_DEBUG("[remote_play] ConfirmDeviceRegist unresolved");
      terminal_reason = "symbol_unresolved";
      break;
    }
    const int err = sceRemoteplayConfirmDeviceRegist(&pair_stat, &pair_err);
    if (err != 0) {
      LOG_DEBUG("[remote_play] ConfirmDeviceRegist 0x%x pair_stat=%d "
                  "pair_err=%d",
                  err, pair_stat, pair_err);
      notify("sceRemoteplayConfirmDeviceRegist 0x%X pair_stat: %d pair_err: %d",
             err, pair_stat, pair_err);
      terminal_reason = "confirmation_error";
      break;
    }
    if (pair_stat != last_pair_stat || pair_err != last_pair_err) {
      LOG_INFO("[remote_play] pairing state status=%d error=%d", pair_stat,
               pair_err);
      last_pair_stat = pair_stat;
      last_pair_err = pair_err;
    }
    if (pair_stat == 2) {
      /*
       * Registration finished. Drop out of PIN/regist mode so the client can
       * open a Remote Play session immediately without waiting for page exit.
       */
      LOG_INFO("[remote_play] pair_stat=2 paired; ending PIN session");
      terminal_reason = "pair_success";
      should_return_to_previous_page = true;
      back_reason = RemotePlayPageBackReason::PairingSucceeded;
      break;
    }
    /* Avoid busy-spinning sceRemoteplayConfirmDeviceRegist. */
    usleep(100 * 1000);
  }

  if (terminal_reason != nullptr &&
      g_session_active.exchange(false, std::memory_order_acq_rel)) {
    invalidate_pin_registration(terminal_reason);
    g_pairing_deadline_ms.store(0, std::memory_order_release);
    if (back_reason == RemotePlayPageBackReason::PairingSucceeded)
      NotifyRemotePlayPaired();
    else if (should_return_to_previous_page)
      NotifyRemotePlayPairingTimedOut();

    if (should_return_to_previous_page)
      RequestRemotePlayPageBack(back_reason);
  }
  LOG_INFO("[remote_play] ConfirmRegistLoop exit reason=%s",
           terminal_reason != nullptr ? terminal_reason : "requested");
  return nullptr;
}

} // namespace
