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
  bool clear_hud_backdrop = false;
  bool capture_ui = false;
  int eye_index = 0;
  float hud_transform[4]={1,1,0,0};
  StereoShadows* shadows=nullptr;
  double scene_copy_ms=0;
  uint32_t frustum_skips=0,world_draws=0;
  bool share_preparation=true;
  uint32_t mesh_reuses=0,material_reuses=0;
};
void InitializeGameVr();
void ShutdownGameVr();
void RecenterGameVr();
void CaptureGameVr();
void ToggleGameVrColor();
void ToggleGameVrCulling();
void CycleGameVrMode();
bool GameVrEnabled();
uint32_t GameVrShadowReferenceHeight(uint32_t fallback);
void SetGameVrSettingsVisible(bool visible);
void PresentFlatMenu(const rex::graphics::NativeGuestOutputRenderContext& context);
bool RenderStereo(const rex::graphics::NativeGuestOutputRenderContext& context,
                  std::shared_ptr<const native_scene::FrameScene> scene,
                  rex::graphics::NativeGuestOutputRenderer render);
}
