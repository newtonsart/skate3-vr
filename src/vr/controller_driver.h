#pragma once
#include "controller_mapping.h"
#include <memory>
#include <rex/input/input_driver.h>

namespace skate3::vr {
void PublishVrControls(const TouchControls& controls,bool suppress_shoulders=false);
std::unique_ptr<rex::input::InputDriver> CreateVrControllerDriver();
}
