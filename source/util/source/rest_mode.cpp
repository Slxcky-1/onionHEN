/* Copyright (C) 2025 OnionHEN / LightningMods

Production wiring for rest-mode recovery (see rest_mode.hpp for the design).

This file owns the platform-specific pieces that keep rest_mode.hpp pure:
- the SIGCONT signal handler (resume event),
- the concrete NetworkProbe / ToolboxReinjector implementations, and
- the util IPC listener re-creation performed on resume.
*/

#include "rest_mode.hpp"
#include "util_toolbox.h"

#include <onion/net.h>
#include <onion/platform.h>

#include <signal.h>

// Re-create the util IPC listener after a standby resume (defined in msg.cpp).
void restart_util_ipc_server();

namespace onion::rest_mode {

namespace {

// The only thing safe to touch from an async signal handler.
volatile sig_atomic_t g_resume_pending = 0;

// Concrete probe over the shared libonion_platform net helper.
class SceNetCtlProbe final : public NetworkProbe {
 public:
  bool is_up() const override { return onion_net_has_ipv4(); }
};

// Concrete reinjector: delegate to the util -> crit-daemon toolbox path.
class DaemonToolboxReinjector final : public ToolboxReinjector {
 public:
  void reinject(bool rest_resume) override { toolbox_reinject(rest_resume); }
};

SceNetCtlProbe g_probe;
DaemonToolboxReinjector g_reinjector;
Recovery g_recovery{g_probe, g_reinjector};

// FreeBSD delivers SIGCONT to the suspended process when the console wakes
// from rest mode, so this handler is the resume event.
void on_sigcont(int /*signo*/) { g_resume_pending = 1; }

}  // namespace

void install() {
  struct sigaction action {};
  action.sa_handler = on_sigcont;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  sigaction(SIGCONT, &action, nullptr);
}

void on_standby() { g_recovery.on_standby(); }

Action poll() {
  if (g_resume_pending != 0) {
    g_resume_pending = 0;
    g_recovery.on_resume();
    // The util IPC listener may be dead after standby; re-create it so
    // ShellUI / daemon clients can connect again.
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