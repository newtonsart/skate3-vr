#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace skate3::vr {
struct TouchControls {
  bool connected=false;
  bool a=false,b=false,x=false,y=false,menu=false;
  bool click[2]{};
  float stick[2][2]{},trigger[2]{},grip[2]{};
};
struct ControllerPad {
  bool connected=false;
  uint16_t buttons=0;
  uint8_t lt=0,rt=0;
  int16_t lx=0,ly=0,rx=0,ry=0;
  bool operator==(const ControllerPad&) const = default;
};
inline float ControllerAxis(float value) {
  return std::isfinite(value) ? std::clamp(value,-1.f,1.f) : 0.f;
}
inline ControllerPad MapTouchControls(const TouchControls& input) {
  ControllerPad pad;
  if (!input.connected) return pad;
  pad.connected=true;
  auto axis=[](float value) { value=ControllerAxis(value); return int16_t(std::lround(value*(value<0 ? 32768.f : 32767.f))); };
  auto trigger=[](float value) { return uint8_t(std::lround(std::max(0.f,ControllerAxis(value))*255)); };
  pad.lx=axis(input.stick[0][0]); pad.ly=axis(input.stick[0][1]);
  pad.rx=axis(input.stick[1][0]); pad.ry=axis(input.stick[1][1]);
  pad.lt=trigger(input.trigger[0]); pad.rt=trigger(input.trigger[1]);
  // Xbox protocol masks; Menu+X provides Back without using the reserved Meta button.
  const bool back=input.menu && input.x;
  if (input.a) pad.buttons|=0x1000;
  if (input.b) pad.buttons|=0x2000;
  if (input.x && !back) pad.buttons|=0x4000;
  if (input.y) pad.buttons|=0x8000;
  if (input.menu) pad.buttons|=back ? 0x0020 : 0x0010;
  if (ControllerAxis(input.grip[0])>0.55f) pad.buttons|=0x0100;
  if (ControllerAxis(input.grip[1])>0.55f) pad.buttons|=0x0200;
  if (input.click[1]) pad.buttons|=0x0080;
  if (input.click[0]) {
    // Click+tilt borrows the left stick for the missing D-pad; centred click remains L3.
    if (ControllerAxis(input.stick[0][1])>0.5f) pad.buttons|=0x0001;
    if (ControllerAxis(input.stick[0][1])< -0.5f) pad.buttons|=0x0002;
    if (ControllerAxis(input.stick[0][0])< -0.5f) pad.buttons|=0x0004;
    if (ControllerAxis(input.stick[0][0])>0.5f) pad.buttons|=0x0008;
    if (pad.buttons&0x000f) { pad.lx=pad.ly=0; }
    else pad.buttons|=0x0040;
  }
  return pad;
}
}
