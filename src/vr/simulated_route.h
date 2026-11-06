#pragma once
#include "controller_mapping.h"

namespace skate3::vr {
constexpr double kSimulatedRouteSeconds=90;
inline TouchControls SimulatedRouteControls(double seconds) {
  TouchControls controls;
  if (!std::isfinite(seconds) || seconds<0 || seconds>=kSimulatedRouteSeconds) return controls;
  controls.connected=true;
  controls.a=std::fmod(seconds,0.6)<0.22;
  controls.stick[0][0]=(seconds>=12 && seconds<20) ? 0.45f :
      (seconds>=32 && seconds<40) ? -0.45f : (seconds>=60 && seconds<70) ? 0.35f : 0.f;
  return controls;
}
}
