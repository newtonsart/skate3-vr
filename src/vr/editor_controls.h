#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace skate3::vr {
enum class EditorMode { None, Window, Camera };
enum class EditorResult { Editing, Apply, Cancel };
struct EditorValues {
  float horizontal=0, height=0, distance=2, width=2.4f;
  float camera_distance=0, camera_height=0;
};
struct EditorInput {
  float horizontal=0, vertical=0, depth=0, resize=0;
  bool apply=false, cancel=false;
};
inline float EditorAxis(int16_t value) {
  const float normalized=std::clamp(float(value)/32767,-1.0f,1.0f);
  return std::copysign(std::max(0.0f,std::abs(normalized)-0.2f)/0.8f,normalized);
}
struct EditorControls {
  EditorMode mode=EditorMode::None;
  EditorValues original, current;
  bool armed=false;
  void Begin(EditorMode next,EditorValues values) {
    mode=next; original=current=values; armed=false;
  }
  EditorResult Update(const EditorInput& input,float dt) {
    if (!armed) {
      // Opening the editor must not reuse the menu's held A button or stick.
      armed=!input.apply && !input.cancel && input.horizontal==0 &&
          input.vertical==0 && input.depth==0 && input.resize==0;
      return EditorResult::Editing;
    }
    if (input.cancel) { current=original; return EditorResult::Cancel; }
    if (input.apply) return EditorResult::Apply;
    if (!std::isfinite(dt) || dt<=0) return EditorResult::Editing;
    dt=std::min(dt,0.05f);
    const auto axis=[](float value) { return std::isfinite(value) ? std::clamp(value,-1.f,1.f) : 0.f; };
    if (mode==EditorMode::Window) {
      current.horizontal=std::clamp(current.horizontal+axis(input.horizontal)*dt,-8.f,8.f);
      current.height=std::clamp(current.height+axis(input.vertical)*dt,-2.f,2.f);
      current.distance=std::clamp(current.distance-axis(input.depth)*dt,0.5f,8.f);
      current.width=std::clamp(current.width+axis(input.resize)*dt,0.5f,6.f);
    } else if (mode==EditorMode::Camera) {
      current.camera_distance=std::clamp(current.camera_distance-axis(input.depth)*dt,-2.f,8.f);
      current.camera_height=std::clamp(current.camera_height+axis(input.vertical)*dt,-2.f,4.f);
    }
    return EditorResult::Editing;
  }
};
struct EditorRegion { uint32_t x,y,width,height; };
inline EditorRegion EditorPanelRegion(uint32_t width,uint32_t height) {
  return {width*6/100,height*74/100,std::max(1u,width*88/100),std::max(1u,height*22/100)};
}
}
