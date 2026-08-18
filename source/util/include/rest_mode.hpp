/* Copyright (C) 2025 OnionHEN / LightningMods

Rest-mode recovery coordination for onion_util.elf.

When the console wakes from rest mode, the FreeBSD kernel delivers SIGCONT to
the suspended process. This module treats that signal as the resume event, then
waits until the network is observed up again before re-injecting the toolbox,
so the reinjection runs against a live network stack. A ShellUI standby report
followed by a network drop/return is a second trigger for consoles where the
resume signal is not delivered.

SOLID layout:
- NetworkProbe / ToolboxReinjector are one-method abstractions (Interface
  Segregation). The recovery logic depends on those abstractions, not on
  sceNetCtl or the crit daemon (Dependency Inversion), so it is deterministic
  and host-testable without the PS5 SDK.
- Recovery is a single-responsibility state machine: it only decides *when* to
  re-inject. Concrete probe/reinjector implementations and the SIGCONT signal
  wiring live in rest_mode.cpp.
*/

#pragma once

namespace onion::rest_mode {

// Result of one poll tick.
enum class Action {
  None,      // nothing to do this tick
  Reinject,  // a toolbox re-injection was dispatched
};

// Network reachability probe (DIP). The recovery logic never calls
// sceNetCtlGetInfo directly.
class NetworkProbe {
 public:
  virtual ~NetworkProbe() = default;
  virtual bool is_up() const = 0;
};

// Toolbox re-injection side effect (DIP). The recovery logic never calls
// enable_toolbox() directly.
class ToolboxReinjector {
 public:
  virtual ~ToolboxReinjector() = default;
  // @param rest_resume true only for rest-mode recovery; drives the optional
  //        rest_mode.resume_reinject_delay_seconds policy.
  virtual void reinject(bool rest_resume) = 0;
};

// Rest-mode recovery state machine.
//
// Events feed it (on_standby / on_resume); the main loop ticks it (poll).
// It re-injects exactly once per rest-mode cycle, and only once the network is
// observed up again so the reinjector runs against a live network stack.
class Recovery {
 public:
  Recovery(NetworkProbe& probe, ToolboxReinjector& reinjector)
      : probe_(probe), reinjector_(reinjector) {}

  // ShellUI reported it is entering standby.
  void on_standby() { standby_reported_ = true; }

  // A resume signal (SIGCONT) was observed.
  void on_resume() { resume_reported_ = true; }

  Action poll() {
    const bool up = probe_.is_up();

    if (!up) {
      // Track the drop only while a standby was reported, so a plain network
      // blip (no rest-mode entry) never arms recovery.
      if (standby_reported_) {
        network_dropped_while_armed_ = true;
      }
      return Action::None;
    }

    const bool should_recover =
        resume_reported_ || network_dropped_while_armed_;

    if (!should_recover) {
      return Action::None;
    }

    // Network is back after a rest-mode cycle: re-inject once, then disarm.
    reinjector_.reinject(/*rest_resume=*/true);
    reset();
    return Action::Reinject;
  }

  void reset() {
    standby_reported_ = false;
    resume_reported_ = false;
    network_dropped_while_armed_ = false;
  }

  bool is_armed() const { return standby_reported_ || resume_reported_; }

 private:
  NetworkProbe& probe_;
  ToolboxReinjector& reinjector_;
  bool standby_reported_ = false;
  bool resume_reported_ = false;
  bool network_dropped_while_armed_ = false;
};

// Production wiring (implemented in rest_mode.cpp).
void install();
void on_standby();
Action poll();

}  // namespace onion::rest_mode