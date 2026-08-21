/* Copyright (C) 2025 OnionHEN / LightningMods

Rest-mode recovery for onion_util.elf.

Wake event is SIGCONT (same as ps5-payload-manager). Recovery only decides
when to re-inject the toolbox; a failed inject stays armed for the next poll.
The optional rest_mode.resume_reinject_delay_seconds wait is applied once per
cycle by the production reinjector, not by this state machine.

SOLID: ToolboxReinjector is a one-method seam so Recovery is host-testable
without the crit daemon. SIGCONT, IPC re-bind, and delay live in rest_mode.cpp.
*/

#pragma once

namespace onion::rest_mode {

enum class Action {
  None,      // nothing to do this tick
  Reinject,  // toolbox re-injection succeeded
};

class ToolboxReinjector {
 public:
  virtual ~ToolboxReinjector() = default;
  // rest_resume is true only for rest-mode recovery (drives the delay policy).
  // Return true on success; false keeps Recovery armed.
  virtual bool reinject(bool rest_resume) = 0;
};

class Recovery {
 public:
  explicit Recovery(ToolboxReinjector& reinjector) : reinjector_(reinjector) {}

  void on_resume() { resume_reported_ = true; }

  Action poll() {
    if (!resume_reported_) {
      return Action::None;
    }
    if (!reinjector_.reinject(/*rest_resume=*/true)) {
      return Action::None;
    }
    reset();
    return Action::Reinject;
  }

  void reset() { resume_reported_ = false; }

  bool is_armed() const { return resume_reported_; }

 private:
  ToolboxReinjector& reinjector_;
  bool resume_reported_ = false;
};

void install();
Action poll();

}  // namespace onion::rest_mode
