#include "openxr_context.h"
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace skate3::vr {
namespace {
void Check(XrResult result, const char* operation) {
  if (XR_FAILED(result)) {
    throw std::runtime_error(std::string(operation) + " failed: XrResult=" +
                             std::to_string(result));
  }
}
}  // namespace

OpenXrContext::~OpenXrContext() {
  if (instance_ != XR_NULL_HANDLE) xrDestroyInstance(instance_);
}

void OpenXrContext::Initialize() {
  if (instance_ != XR_NULL_HANDLE) throw std::logic_error("OpenXR already initialized");
  uint32_t count = 0;
  Check(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr), "Enumerate extensions");
  std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
  Check(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data()), "Read extensions");
  constexpr const char* vulkan = "XR_KHR_vulkan_enable2";
  if (std::none_of(extensions.begin(), extensions.end(), [&](const auto& extension) {
        return std::strcmp(extension.extensionName, vulkan) == 0;
      })) throw std::runtime_error("Runtime lacks XR_KHR_vulkan_enable2");
  XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
  std::snprintf(info.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Skate3Recomp VR");
  info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
  info.enabledExtensionCount = 1;
  info.enabledExtensionNames = &vulkan;
  Check(xrCreateInstance(&info, &instance_), "Create instance");
  XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
  Check(xrGetInstanceProperties(instance_, &properties), "Get runtime properties");
  runtime_ = properties.runtimeName;
}

void OpenXrContext::DetectHeadset() {
  if (instance_ == XR_NULL_HANDLE) throw std::logic_error("OpenXR not initialized");
  XrSystemGetInfo info{XR_TYPE_SYSTEM_GET_INFO};
  info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  XrSystemId system = XR_NULL_SYSTEM_ID;
  Check(xrGetSystem(instance_, &info, &system), "Detect HMD (connect Quest Link and activate PCVR)");
  XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
  Check(xrGetSystemProperties(instance_, system, &properties), "Get HMD properties");
  uint32_t count = 0;
  Check(xrEnumerateViewConfigurationViews(instance_, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        0, &count, nullptr), "Enumerate stereo views");
  if (count != 2) throw std::runtime_error("Runtime did not report two primary stereo views");
  std::vector<XrViewConfigurationView> views(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
  Check(xrEnumerateViewConfigurationViews(instance_, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        count, &count, views.data()), "Read stereo view dimensions");
  PFN_xrVoidFunction function = nullptr;
  Check(xrGetInstanceProcAddr(instance_, "xrGetVulkanGraphicsRequirements2KHR", &function), "Find Vulkan requirements");
  XrGraphicsRequirementsVulkan2KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
  Check(reinterpret_cast<PFN_xrGetVulkanGraphicsRequirements2KHR>(function)(instance_, system, &requirements), "Get Vulkan requirements");
  system_ = system;
  headset_ = properties.systemName;
  views_ = std::move(views);
  min_vulkan_ = requirements.minApiVersionSupported;
  max_vulkan_ = requirements.maxApiVersionSupported;
}
}  // namespace skate3::vr
