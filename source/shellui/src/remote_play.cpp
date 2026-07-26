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
#include <onion/account_id_b64.h>
#include <unistd.h>

bool IsRunningConfirmRegistLoop = false;
pthread_t ConfirmRegistLoop_Thread;

namespace {
bool g_confirm_thread_started = false;

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
  LOG_ERROR("[remote_play] NotifyPinCodeError(1) => 0x%x (%s)", err, why);
}

} // namespace

void Base64Encode(uint64_t input, char *output) {
  onion_account_id_base64_encode(input, output);
}

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
  pin = 0;

  /* Join prior confirm thread; StopConfirmRegistLoop also invalidates PIN. */
  StopConfirmRegistLoop();

  if (!sceRemoteplayGeneratePinCode) {
    LOG_DEBUG("[remote_play] GeneratePinCode unresolved");
    return false;
  }

  const int err = sceRemoteplayGeneratePinCode(&pin);
  if (err != 0) {
    LOG_ERROR("[remote_play] GeneratePinCode failed => 0x%x", err);
    return false;
  }

  const int thread_err =
      pthread_create(&ConfirmRegistLoop_Thread, nullptr, ConfirmRegistLoop,
                     nullptr);
  if (thread_err != 0) {
    LOG_ERROR("[remote_play] confirm thread create failed => 0x%x",
                thread_err);
    invalidate_pin_registration("thread_create_failed");
    return false;
  }
  g_confirm_thread_started = true;

  return true;
}

void StopConfirmRegistLoop() {
  const bool was_running = IsRunningConfirmRegistLoop;
  const bool was_started = g_confirm_thread_started;

  if (was_running || was_started) {
    IsRunningConfirmRegistLoop = false;
    if (was_started) {
      void *retval = nullptr;
      pthread_join(ConfirmRegistLoop_Thread, &retval);
      g_confirm_thread_started = false;
    }
    LOG_DEBUG("[remote_play] confirm loop stopped (was_running=%d)",
                was_running ? 1 : 0);
  }

  /*
   * Always leave registration mode when the toolbox RP page is torn down or a
   * new PIN is about to be generated. Stopping the poll thread alone is not
   * enough — libSceRemoteplay keeps the PIN session until NotifyPinCodeError.
   */
  invalidate_pin_registration(was_running || was_started ? "stop_loop"
                                                         : "stop_idle");
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
  Base64Encode(accountid, buff);
  return buff[0] != '\0';
}

void *ConfirmRegistLoop(void *) {
  IsRunningConfirmRegistLoop = true;
  int pair_stat = -1, pair_err = -1, err = -1;

  LOG_DEBUG("[remote_play] ConfirmRegistLoop started");

  while (IsRunningConfirmRegistLoop) {
    if (!sceRemoteplayConfirmDeviceRegist) {
      LOG_DEBUG("[remote_play] ConfirmDeviceRegist unresolved");
      break;
    }
    err = sceRemoteplayConfirmDeviceRegist(&pair_stat, &pair_err);
    if (err != 0) {
      LOG_DEBUG("[remote_play] ConfirmDeviceRegist 0x%x pair_stat=%d "
                  "pair_err=%d",
                  err, pair_stat, pair_err);
      notify("sceRemoteplayConfirmDeviceRegist 0x%X pair_stat: %d pair_err: %d",
             err, pair_stat, pair_err);
      break;
    }
    if (pair_stat == 2) {
      /*
       * Registration finished. Drop out of PIN/regist mode so the client can
       * open a Remote Play session immediately without waiting for page exit.
       */
      LOG_DEBUG("[remote_play] pair_stat=2 paired — ending PIN session");
      notify("Remote Play paired! For better stability a reboot is recommended");
      invalidate_pin_registration("pair_success");
      break;
    }
    /* Avoid busy-spinning sceRemoteplayConfirmDeviceRegist. */
    usleep(100 * 1000);
  }

  IsRunningConfirmRegistLoop = false;
  LOG_DEBUG("[remote_play] ConfirmRegistLoop exit");
  return nullptr;
}
