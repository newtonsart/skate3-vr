#pragma once
#include <memory>
#include "skate3_native_scene.h"
#include <rex/graphics/native_guest_renderer.h>

namespace skate3::vr {
struct StereoShadows {
  bool built=false;
  bool ready=false;
};
struct EyeCamera {
  std::shared_ptr<const native_scene::FrameScene> scene;
  std::shared_ptr<native_scene::FrameScene> render_scene;
  float view_proj[16]{}, proj[16]{}, position[3]{};
  bool override_camera = false;
  int eye_index = 0;
  float hud_transform[4]={1,1,0,0};
  StereoShadows* shadows=nullptr;
  double scene_copy_ms=0;
};
void InitializeGameVr();
void ShutdownGameVr();
void RecenterGameVr();
void CaptureGameVr();
void ToggleGameVrColor();
void ToggleGameVrCulling();
void CycleGameVrMode();
bool GameVrEnabled();
void PresentFlatMenu(const rex::graphics::NativeGuestOutputRenderContext& context);
bool RenderStereo(const rex::graphics::NativeGuestOutputRenderContext& context,
                  std::shared_ptr<const native_scene::FrameScene> scene,
                  rex::graphics::NativeGuestOutputRenderer render);
}
