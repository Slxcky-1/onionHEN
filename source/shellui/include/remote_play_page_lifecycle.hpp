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
  bool manager_active;
  bool is_current_page;
  bool actual_visible;
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
  const bool visible = observation.manager_active &&
                       observation.is_current_page &&
                       observation.actual_visible;

  if (phase == PagePhase::Inactive)
    return PageObservationAction::Wait;
  if (phase == PagePhase::Loading)
    return visible ? PageObservationAction::MarkVisible
                   : PageObservationAction::Wait;
  if (!observation.manager_active)
    return PageObservationAction::LeaveToolbox;
  if (!observation.is_current_page)
    return PageObservationAction::LeaveToPrevious;
  if (!observation.actual_visible)
    return PageObservationAction::LeaveToolbox;
  return PageObservationAction::StayVisible;
}

} // namespace remote_play
