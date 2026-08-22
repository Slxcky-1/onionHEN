/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Process-wide resume coordination.  The system-state event flag is the
 * resume source; service recovery and ShellUI reconciliation remain owned by
 * their respective modules.
 */

#include "daemon_ops.hpp"
#include "daemon_power_state.hpp"
#include <onion/platform.h>
#include <atomic>
#include <unistd.h>

namespace {

bool listeners_ready() {
  return control_tcp_is_listening() && crit_ipc_is_listening();
}

} // namespace

void *resume_recovery_thread(void *args) noexcept {
  (void)args;
  bool was_sleeping = false;
  bool recovery_active = false;
  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    const DaemonPowerState power_state = daemon_power_state_get();
    if (daemon_power_state_is_sleeping(power_state)) {
      was_sleeping = true;
    }
    const bool state_woke = was_sleeping &&
                            power_state == DaemonPowerState::Working;
    if (!recovery_active && state_woke) {
      recovery_active = true;
      LOG_INFO("rest: WORKING transition; starting recovery transaction");

      if (daemon_power_state_wait_working(/*timeout_ms=*/30000)) {
        /* Network interfaces and loopback sockets need time to settle. */
        usleep(1000 * 1000);
        restart_crit_ipc_server();
        control_tcp_restart();

        bool ready = false;
        for (int i = 0; i < 30; ++i) {
          if (listeners_ready()) {
            ready = true;
            break;
          }
          usleep(100 * 1000);
        }
        LOG_INFO("rest: listener recovery %s (tcp9048=%d crit_ipc=%d)",
                 ready ? "ready" : "pending",
                 control_tcp_is_listening() ? 1 : 0,
                 crit_ipc_is_listening() ? 1 : 0);

        /* NOTE_EXEC remains authoritative; this is only compensation. */
        toolbox_on_resume();
      } else {
        LOG_WARN("rest: power state did not return to WORKING; skip compensation");
      }
      recovery_active = false;
      was_sleeping = false;
    }
    usleep(100 * 1000);
  }
  return nullptr;
}
