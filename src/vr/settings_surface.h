#pragma once
#include <vulkan/vulkan.h>
#include <mutex>
#include "presentation.h"
namespace rex::ui { class UIDrawContext; }
namespace skate3::vr {
struct SettingsImage {
  VkImage image=VK_NULL_HANDLE;
  VkImageView view=VK_NULL_HANDLE;
  uint32_t width=0,height=0;
  bool srgb=false;
  int editor_mode=0;
  bool system_dialog=false;
  bool fps_only=false;
  FpsPanelLayout fps_region;
  uint32_t generation=0;
};
void InitializeSettingsSurface();
void ShutdownSettingsSurface();
rex::ui::UIDrawContext* BeginSettingsSurface(rex::ui::UIDrawContext& desktop,bool system_dialog=false,bool fps_only=false);
void EndSettingsSurface();
// The caller keeps the lock until its GPU copy has completed. Never wait for
// the UI thread while holding the game's render state or presenter locks.
std::unique_lock<std::mutex> TryReadSettingsSurface(SettingsImage& image);
}
