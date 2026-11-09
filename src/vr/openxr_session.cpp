#include <chrono>
#include "openxr_session.h"
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr_platform.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace skate3::vr {
namespace {
void Check(XrResult result, const char* operation) {
  if (XR_FAILED(result)) throw std::runtime_error(std::string(operation) +
      " failed: XrResult=" + std::to_string(result));
}
void CheckVk(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) +
      " failed: VkResult=" + std::to_string(result));
}
template <typename Function>
Function Load(XrInstance instance, const char* name) {
  PFN_xrVoidFunction function = nullptr;
  Check(xrGetInstanceProcAddr(instance, name, &function), name);
  if (!function) throw std::runtime_error(std::string("Missing function: ") + name);
  return reinterpret_cast<Function>(function);
}
}  // namespace

OpenXrSession::OpenXrSession(const OpenXrContext& context) : context_(context) {}
OpenXrSession::~OpenXrSession() { Destroy(); }

void OpenXrSession::Destroy() {
  DrainFrameWait();
  if (wait_worker_.joinable()) {
    wait_worker_.request_stop();
    wait_request_.release();
    wait_worker_.join();
  }
  if (device_ && owns_device_) vkDeviceWaitIdle(device_);
  if (view_) xrDestroySpace(view_);
  if (local_) xrDestroySpace(local_);
  if (session_) xrDestroySession(session_);
  if (device_ && owns_device_) vkDestroyDevice(device_, nullptr);
  if (vk_instance_ && owns_device_) vkDestroyInstance(vk_instance_, nullptr);
  view_ = local_ = XR_NULL_HANDLE;
  session_ = XR_NULL_HANDLE;
  device_ = VK_NULL_HANDLE;
  vk_instance_ = VK_NULL_HANDLE;
  running_ = false;
  queue_mutex_ = nullptr;
}

void OpenXrSession::Create(uint32_t api_version) {
  if (vk_instance_ || !context_.system()) throw std::logic_error("Invalid session initialization");
  try {
    owns_device_ = true;
    const auto instance = context_.instance();
    const auto system = context_.system();
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Skate3Recomp VR";
    app.apiVersion = api_version;
    VkInstanceCreateInfo vk_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    vk_info.pApplicationInfo = &app;
    XrVulkanInstanceCreateInfoKHR info{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    info.systemId = system;
    info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    info.vulkanCreateInfo = &vk_info;
    VkResult vk_result = VK_SUCCESS;
    Check(Load<PFN_xrCreateVulkanInstanceKHR>(instance, "xrCreateVulkanInstanceKHR")(
        instance, &info, &vk_instance_, &vk_result), "Create XR Vulkan instance");
    CheckVk(vk_result, "Create Vulkan instance");
    XrVulkanGraphicsDeviceGetInfoKHR gpu_info{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    gpu_info.systemId = system;
    gpu_info.vulkanInstance = vk_instance_;
    Check(Load<PFN_xrGetVulkanGraphicsDevice2KHR>(instance, "xrGetVulkanGraphicsDevice2KHR")(
        instance, &gpu_info, &physical_device_), "Select runtime GPU");
    vkGetPhysicalDeviceProperties(physical_device_, &gpu_properties_);
    if (gpu_properties_.apiVersion < api_version) throw std::runtime_error("GPU lacks requested Vulkan API");
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &count, queues.data());
    uint32_t family = 0;
    while (family < count && !(queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    if (family == count) throw std::runtime_error("No graphics queue on runtime GPU");
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue;
    XrVulkanDeviceCreateInfoKHR xr_device{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xr_device.systemId = system;
    xr_device.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xr_device.vulkanPhysicalDevice = physical_device_;
    xr_device.vulkanCreateInfo = &device_info;
    Check(Load<PFN_xrCreateVulkanDeviceKHR>(instance, "xrCreateVulkanDeviceKHR")(
        instance, &xr_device, &device_, &vk_result), "Create XR Vulkan device");
    CheckVk(vk_result, "Create Vulkan device");
    CreateSpaces(family);
  } catch (...) {
    Destroy();
    throw;
  }
}

void OpenXrSession::Bind(VkInstance instance, VkPhysicalDevice gpu, VkDevice device, uint32_t family,
                       std::recursive_mutex* queue_mutex) {
  if (vk_instance_ || !context_.system() || !instance || !gpu || !device)
    throw std::logic_error("Invalid external Vulkan binding");
  vk_instance_ = instance;
  physical_device_ = gpu;
  device_ = device;
  owns_device_ = false;
  queue_mutex_ = queue_mutex;
  vkGetPhysicalDeviceProperties(gpu, &gpu_properties_);
  try { CreateSpaces(family); } catch (...) { Destroy(); throw; }
}

std::unique_lock<std::recursive_mutex> OpenXrSession::LockQueue() const {
  if (queue_mutex_) return std::unique_lock<std::recursive_mutex>(*queue_mutex_);
  return {};
}

void OpenXrSession::CreateSpaces(uint32_t family) {
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = vk_instance_;
    binding.physicalDevice = physical_device_;
    binding.device = device_;
    binding.queueFamilyIndex = family;
    binding.queueIndex = 0;
    XrSessionCreateInfo session{XR_TYPE_SESSION_CREATE_INFO};
    session.next = &binding;
    session.systemId = context_.system();
    Check(xrCreateSession(context_.instance(), &session, &session_), "Create Vulkan XR session");
    XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.poseInReferenceSpace.orientation.w = 1.0f;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    Check(xrCreateReferenceSpace(session_, &space, &local_), "Create local space");
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    Check(xrCreateReferenceSpace(session_, &space, &view_), "Create head space");
}

bool OpenXrSession::PollEvents() {
  XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
  for (;;) {
    const auto result = xrPollEvent(context_.instance(), &event);
    if (result == XR_EVENT_UNAVAILABLE) break;
    Check(result, "Poll XR event");
    if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) exiting_ = true;
    if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
      const auto& change = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
      if (change.session == session_) {
        state_ = change.state;
        if (state_ == XR_SESSION_STATE_READY && !running_) {
          XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
          begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
          Check(xrBeginSession(session_, &begin), "Begin stereo session");
          running_ = true;
        } else if (state_ == XR_SESSION_STATE_STOPPING && running_) {
          DrainFrameWait();
          Check(xrEndSession(session_), "End session");
          running_ = false;
        } else if (state_ == XR_SESSION_STATE_EXITING || state_ == XR_SESSION_STATE_LOSS_PENDING) {
          exiting_ = true;
        }
      }
    }
    event = {XR_TYPE_EVENT_DATA_BUFFER};
  }
  if (exiting_) DrainFrameWait();
  return !exiting_;
}

XrResult OpenXrSession::WaitFrame(XrFrameState& state) {
  XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
  return xrWaitFrame(session_, &wait, &state);
}

void OpenXrSession::PrefetchFrame() {
  if (!running_ || exiting_ || wait_pending_) return;
  // Only the wait runs here. The render thread consumes its state before beginning and locating views.
  if (!wait_worker_.joinable()) {
    wait_worker_=std::jthread([this](std::stop_token stop) {
      for (;;) {
        wait_request_.acquire();
        if (stop.stop_requested()) return;
        prefetched_state_={XR_TYPE_FRAME_STATE};
        wait_started_=std::chrono::steady_clock::now();
        prefetched_result_=WaitFrame(prefetched_state_);
        wait_ready_=std::chrono::steady_clock::now();
        wait_complete_.release();
      }
    });
  }
  wait_pending_=true;
  wait_request_.release();
}

void OpenXrSession::DrainFrameWait() {
  if (wait_pending_) {
    wait_complete_.acquire();
    wait_pending_=false;
  }
}

bool OpenXrSession::BeginFrame(TrackingFrame& frame) {
  if (!running_ || exiting_) return false;
  if (frame_started_) throw std::logic_error("XR frame already begun");
  XrFrameState state{XR_TYPE_FRAME_STATE};
  const auto wait_start = std::chrono::steady_clock::now();
  const bool prefetched=wait_pending_;
  if (prefetched) {
    DrainFrameWait();
    Check(prefetched_result_, "Wait predicted frame");
    state=prefetched_state_;
  } else {
    Check(WaitFrame(state), "Wait predicted frame");
  }
  const auto begin_start = std::chrono::steady_clock::now();
  XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
  {
    auto queue_lock = LockQueue();
    Check(xrBeginFrame(session_, &begin), "Begin frame");
  }
  frame_started_ = true;
  const auto tracking_start = std::chrono::steady_clock::now();
  frame = {};
  frame.wait_ms = std::chrono::duration<double, std::milli>(begin_start - wait_start).count();
  frame.prefetched=prefetched;
  frame.wait_call_ms=prefetched ? std::chrono::duration<double,std::milli>(wait_ready_-wait_started_).count() : frame.wait_ms;
  frame.wait_ready_age_ms=prefetched ? std::chrono::duration<double,std::milli>(begin_start-wait_ready_).count() : 0;
  frame.begin_ms = std::chrono::duration<double, std::milli>(tracking_start - begin_start).count();
  frame.display_time = state.predictedDisplayTime;
  frame.display_period = state.predictedDisplayPeriod;
  frame.should_render = state.shouldRender == XR_TRUE;
  auto head_result = xrLocateSpace(view_, local_, state.predictedDisplayTime, &frame.head);
  XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
  locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  locate.displayTime = state.predictedDisplayTime;
  locate.space = local_;
  XrViewState views{XR_TYPE_VIEW_STATE};
  uint32_t count = 0;
  auto eyes_result = xrLocateViews(session_, &locate, &views, 2, &count, frame.eyes);
  frame.tracking_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tracking_start).count();
  frame.view_flags = views.viewStateFlags;
  if (XR_FAILED(head_result) || XR_FAILED(eyes_result) || count != 2) EndFrame(frame);
  Check(head_result, "Locate head pose");
  Check(eyes_result, "Locate stereo eye poses");
  if (count != 2) throw std::runtime_error("Runtime returned incorrect eye count");
  return true;
}

void OpenXrSession::EndFrame(const TrackingFrame& frame, uint32_t count,
                           const XrCompositionLayerBaseHeader* const* layers) {
  if (!frame_started_) throw std::logic_error("No XR frame to end");
  XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
  end.displayTime = frame.display_time;
  end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end.layerCount = count;
  end.layers = layers;
  frame_started_ = false;
  auto queue_lock = LockQueue();
  Check(xrEndFrame(session_, &end), "End XR frame");
}

bool OpenXrSession::SampleTracking(TrackingFrame& frame) {
  if (!BeginFrame(frame)) return false;
  EndFrame(frame);
  return true;
}

void OpenXrSession::RequestExit() {
  DrainFrameWait();
  if (running_) Check(xrRequestExitSession(session_), "Request session exit");
}
}  // namespace skate3::vr
