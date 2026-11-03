#include "controller_driver.h"
#include <rex/input/input_system.h>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace { void Check(bool ok) { if(!ok) throw std::runtime_error("Controller driver check failed"); } }
int main() {
  using namespace skate3::vr;
  using rex::X_RESULT;
  using namespace rex::input;
  auto driver=CreateVrControllerDriver();
  X_INPUT_STATE state{};
  Check(driver->GetState(0,&state)==X_ERROR_DEVICE_NOT_CONNECTED);
  TouchControls controls; controls.connected=true; controls.a=true;
  controls.stick[0][0]=-1; controls.stick[0][1]=1; controls.trigger[1]=0.5f;
  PublishVrControls(controls);
  Check(driver->GetState(1,&state)==X_ERROR_DEVICE_NOT_CONNECTED);
  Check(driver->GetState(0,&state)==X_ERROR_SUCCESS);
  Check(state.gamepad.buttons==X_INPUT_GAMEPAD_A && state.gamepad.thumb_lx==-32768 && state.gamepad.thumb_ly==32767);
  Check(state.gamepad.right_trigger==128);
  const auto packet=static_cast<uint32_t>(state.packet_number);
  PublishVrControls(controls); driver->GetState(0,&state);
  Check(state.packet_number==packet);
  X_INPUT_CAPABILITIES caps{};
  Check(driver->GetCapabilities(0,0,&caps)==X_ERROR_SUCCESS && caps.type==1 && caps.sub_type==1);
  InputSystem input(nullptr); input.AddDriver(std::move(driver));
  input.SetActiveCallback([] { return false; });
  Check(input.GetState(0,&state)==X_ERROR_SUCCESS && state.gamepad.buttons==0 && state.gamepad.thumb_lx==0);
  X_INPUT_GAMEPAD ui{};
  Check(input.GetUiGamepadState(&ui) && ui.buttons==X_INPUT_GAMEPAD_A && ui.thumb_lx==-32768);
  bool menu=false; input.SetMenuChordCallback([&] { menu=true; });
  controls.menu=true; controls.grip[1]=1; PublishVrControls(controls);
  Check(input.GetState(0,&state)==X_ERROR_SUCCESS && menu);
  PublishVrControls({}); Check(!input.GetUiGamepadState(&ui) && ui.buttons==0);
  PublishVrControls(controls); std::this_thread::sleep_for(std::chrono::milliseconds(270));
  Check(!input.GetUiGamepadState(&ui));
}
