#include "openxr_context.h"
#include <cstdio>
#include <exception>

int main() {
  try {
    skate3::vr::OpenXrContext context;
    context.Initialize();
    std::printf("Runtime: %s\n", context.runtime().c_str());
    context.DetectHeadset();
    std::printf("HMD: %s\n", context.headset().c_str());
    for (size_t eye = 0; eye < context.views().size(); ++eye) {
      const auto& view = context.views()[eye];
      std::printf("Eye %zu: %ux%u recommended, samples %u\n", eye,
          view.recommendedImageRectWidth, view.recommendedImageRectHeight,
          view.recommendedSwapchainSampleCount);
    }
    auto version = [](const char* label, XrVersion value) {
      std::printf("Vulkan %s: %u.%u.%u\n", label, unsigned(XR_VERSION_MAJOR(value)),
          unsigned(XR_VERSION_MINOR(value)), unsigned(XR_VERSION_PATCH(value)));
    };
    version("minimum", context.min_vulkan_version());
    version("maximum", context.max_vulkan_version());
    std::puts("Instance and HMD detected. Session, tracking and stereo rendering remain untested.");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "OpenXR: %s\n", error.what());
    return 1;
  }
}
