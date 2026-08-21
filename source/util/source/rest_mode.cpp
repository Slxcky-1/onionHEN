/* Copyright (C) 2025 OnionHEN / LightningMods

Production wiring for rest-mode recovery (see rest_mode.hpp).

Owns: SIGCONT handler, util Unix IPC re-bind, and toolbox reinjector
(delay once per cycle). Plugin payloads recover themselves.
*/

#include "rest_mode.hpp"
#include "util_toolbox.h"

#include <onion/platform.h>

#include <signal.h>

void restart_util_ipc_server();

namespace onion::rest_mode {

namespace {

volatile sig_atomic_t g_resume_pending = 0;

// Delay once per rest cycle, then delegate to util -> crit-daemon toolbox.
// Retries skip rest_mode.resume_reinject_delay_seconds.
class DaemonToolboxReinjector final : public ToolboxReinjector {
 public:
  void arm_rest_delay() { apply_delay_next_ = true; }
  bool reinject(bool rest_resume) override {
    const bool apply_delay = rest_resume && apply_delay_next_;
    apply_delay_next_ = false;
    return toolbox_reinject(rest_resume, apply_delay);
  }

 private:
  bool apply_delay_next_ = false;
};

DaemonToolboxReinjector g_reinjector;
Recovery g_recovery{g_reinjector};

void on_sigcont(int /*signo*/) { g_resume_pending = 1; }

}  // namespace

void install() {
  struct sigaction action {};
  action.sa_handler = on_sigcont;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  sigaction(SIGCONT, &action, nullptr);
}

Action poll() {
  if (g_resume_pending != 0) {
    g_resume_pending = 0;
    g_recovery.on_resume();
    g_reinjector.arm_rest_delay();
    restart_util_ipc_server();
    LOG_INFO("rest-mode resume signal received");
  }

  const Action action = g_recovery.poll();
  if (action == Action::Reinject) {
    LOG_INFO("rest-mode recovery dispatched toolbox reinject");
  }
  return action;
}

}  // namespace onion::rest_mode
