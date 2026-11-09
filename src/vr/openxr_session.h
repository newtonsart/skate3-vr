#pragma once

#include "openxr_context.h"
#include <vulkan/vulkan.h>
#include <chrono>
#include <mutex>
#include <semaphore>
#include <thread>

namespace skate3::vr {

struct TrackingFrame {
  XrTime display_time = 0;
  XrDuration display_period = 0;
  bool should_render = false;
  double wait_ms=0, begin_ms=0, tracking_ms=0;
  double wait_call_ms=0, wait_ready_age_ms=0;
  bool prefetched=false;
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
  void Bind(VkInstance instance, VkPhysicalDevice gpu, VkDevice device, uint32_t family,
            std::recursive_mutex* queue_mutex = nullptr);
  bool PollEvents();
  void PrefetchFrame();
  void DrainFrameWait();
  bool BeginFrame(TrackingFrame& frame);
  void EndFrame(const TrackingFrame& frame, uint32_t count = 0,
                const XrCompositionLayerBaseHeader* const* layers = nullptr);
  bool SampleTracking(TrackingFrame& frame);
  void RequestExit();
  bool running() const { return running_; }
  XrSessionState state() const { return state_; }
  const char* gpu_name() const { return gpu_properties_.deviceName; }
  XrSession handle() const { return session_; }
  XrSpace local_space() const { return local_; }
  XrSpace view_space() const { return view_; }

 private:
  void Destroy();
  XrResult WaitFrame(XrFrameState& state);
  void CreateSpaces(uint32_t family);
  std::unique_lock<std::recursive_mutex> LockQueue() const;
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
  bool owns_device_ = false;
  bool frame_started_ = false;
  std::recursive_mutex* queue_mutex_ = nullptr;
  XrFrameState prefetched_state_{XR_TYPE_FRAME_STATE};
  XrResult prefetched_result_=XR_SUCCESS;
  std::chrono::steady_clock::time_point wait_started_, wait_ready_;
  bool wait_pending_=false;
  std::binary_semaphore wait_request_{0}, wait_complete_{0};
  std::jthread wait_worker_;
};
}  // namespace skate3::vr
