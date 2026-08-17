/* Copyright (C) 2025 OnionHEN / LightningMods
 * Pure Remote Play page lifecycle policy.
 */
#pragma once

namespace remote_play {

enum class PagePhase : unsigned char {
  Inactive = 0,
  Loading,
  Visible,
  BackRequested,
};

struct PageObservation {
  bool has_focus;
  bool is_current_page;
};

enum class PageObservationAction : unsigned char {
  Wait = 0,
  MarkVisible,
  StayVisible,
  LeaveToPrevious,
  LeaveToolbox,
};

constexpr PageObservationAction
classify_page_observation(PagePhase phase, PageObservation observation) {
  if (phase == PagePhase::Inactive)
    return PageObservationAction::Wait;
  if (phase == PagePhase::Loading)
    return observation.has_focus ? PageObservationAction::MarkVisible
                                 : PageObservationAction::Wait;
  if (observation.has_focus)
    return PageObservationAction::StayVisible;
  return observation.is_current_page
             ? PageObservationAction::LeaveToolbox
             : PageObservationAction::LeaveToPrevious;
}

} // namespace remote_play
