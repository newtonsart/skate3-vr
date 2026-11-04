#pragma once

#include <openxr/openxr.h>
#include <string>
#include <vector>

namespace skate3::vr {

// Owns instance lifetime; no graphics device or session is created by this probe.
class OpenXrContext {
 public:
  OpenXrContext() = default;
  ~OpenXrContext();
  OpenXrContext(const OpenXrContext&) = delete;
  OpenXrContext& operator=(const OpenXrContext&) = delete;
  void Initialize();
  void DetectHeadset();
  XrInstance instance() const { return instance_; }
  XrSystemId system() const { return system_; }
  const std::string& runtime() const { return runtime_; }
  const std::string& headset() const { return headset_; }
  const std::vector<XrViewConfigurationView>& views() const { return views_; }
  XrVersion min_vulkan_version() const { return min_vulkan_; }
  XrVersion max_vulkan_version() const { return max_vulkan_; }
  bool passthrough_extension_enabled() const { return passthrough_extension_enabled_; }
  bool passthrough_supported() const { return passthrough_supported_; }

 private:
  XrInstance instance_ = XR_NULL_HANDLE;
  XrSystemId system_ = XR_NULL_SYSTEM_ID;
  std::string runtime_, headset_;
  std::vector<XrViewConfigurationView> views_;
  XrVersion min_vulkan_ = 0, max_vulkan_ = 0;
  bool passthrough_extension_enabled_ = false;
  bool passthrough_supported_ = false;
};
}  // namespace skate3::vr
