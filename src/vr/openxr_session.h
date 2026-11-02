#pragma once

#include "openxr_context.h"
#include <vulkan/vulkan.h>

namespace skate3::vr {

struct TrackingFrame {
  XrTime display_time = 0;
  XrDuration display_period = 0;
  bool should_render = false;
  XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
  XrViewStateFlags view_flags = 0;
  XrView eyes[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
};

// Diagnostic session owns its Vulkan resources. Gameplay must use the same
// runtime-selected device for both the renderer and the session.
class OpenXrSession {
 public:
  explicit OpenXrSession(const OpenXrContext& context);
  ~OpenXrSession();
  OpenXrSession(const OpenXrSession&) = delete;
  OpenXrSession& operator=(const OpenXrSession&) = delete;
  void Create(uint32_t vulkan_api_version);
  bool PollEvents();
  bool SampleTracking(TrackingFrame& frame);
  void RequestExit();
  bool running() const { return running_; }
  XrSessionState state() const { return state_; }
  const char* gpu_name() const { return gpu_properties_.deviceName; }

 private:
  void Destroy();
  const OpenXrContext& context_;
  VkInstance vk_instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties gpu_properties_{};
  XrSession session_ = XR_NULL_HANDLE;
  XrSpace local_ = XR_NULL_HANDLE;
  XrSpace view_ = XR_NULL_HANDLE;
  XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
  bool running_ = false;
  bool exiting_ = false;
};
}  // namespace skate3::vr
