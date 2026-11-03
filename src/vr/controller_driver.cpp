#include "controller_driver.h"
#include <chrono>
#include <mutex>

namespace skate3::vr {
using rex::X_RESULT;
using rex::X_STATUS;
namespace {
std::mutex cache_mutex;
ControllerPad cached;
std::chrono::steady_clock::time_point updated;
uint32_t packet=0;

class VrControllerDriver final : public rex::input::InputDriver {
 public:
  VrControllerDriver():InputDriver(nullptr,0) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  X_RESULT GetState(uint32_t index,rex::input::X_INPUT_STATE* output) override {
    std::lock_guard lock(cache_mutex);
    // Release held controls if rendering stops before the next OpenXR input sample.
    if(index || !cached.connected || std::chrono::steady_clock::now()-updated>std::chrono::milliseconds(250))
      return X_ERROR_DEVICE_NOT_CONNECTED;
    if(output) {
      *output={}; output->packet_number=packet;
      auto& pad=output->gamepad;
      pad.buttons=cached.buttons; pad.left_trigger=cached.lt; pad.right_trigger=cached.rt;
      pad.thumb_lx=cached.lx; pad.thumb_ly=cached.ly; pad.thumb_rx=cached.rx; pad.thumb_ry=cached.ry;
    }
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetCapabilities(uint32_t index,uint32_t,rex::input::X_INPUT_CAPABILITIES* output) override {
    if(GetState(index,nullptr)!=X_ERROR_SUCCESS) return X_ERROR_DEVICE_NOT_CONNECTED;
    if(output) {
      *output={}; output->type=1; output->sub_type=1;
      output->gamepad.buttons=0xf3ff;
      output->gamepad.left_trigger=output->gamepad.right_trigger=255;
      output->gamepad.thumb_lx=output->gamepad.thumb_ly=32767;
      output->gamepad.thumb_rx=output->gamepad.thumb_ry=32767;
    }
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetState(uint32_t,rex::input::X_INPUT_VIBRATION*) override { return X_ERROR_DEVICE_NOT_CONNECTED; }
  X_RESULT GetKeystroke(uint32_t,uint32_t,rex::input::X_INPUT_KEYSTROKE*) override { return X_ERROR_DEVICE_NOT_CONNECTED; }
};
}
void PublishVrControls(const TouchControls& controls,bool suppress_shoulders) {
  std::lock_guard lock(cache_mutex);
  auto filtered=controls;
  if(suppress_shoulders) filtered.grip[0]=filtered.grip[1]=0;
  const auto sample=MapTouchControls(filtered);
  if(sample!=cached) ++packet;
  cached=sample; updated=std::chrono::steady_clock::now();
}
std::unique_ptr<rex::input::InputDriver> CreateVrControllerDriver() { return std::make_unique<VrControllerDriver>(); }
}
