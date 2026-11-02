#include "openxr_context.h"
#include "openxr_session.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <thread>

int main(int argc, char** argv) {
  try {
    const bool track = argc == 2 && std::strcmp(argv[1], "--track") == 0;
    if (argc > 1 && !track) throw std::runtime_error("Usage: skate3-openxr-probe [--track]");
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
    version("highest tested by runtime", context.max_vulkan_version());
    if (!track) {
      std::puts("Instance and HMD detected. Use --track for a 10-second Vulkan 1.3 session test.");
      return 0;
    }
    skate3::vr::OpenXrSession session(context);
    session.Create(VK_API_VERSION_1_3);
    std::printf("Vulkan 1.3 session created on %s. Tracking only; no game images submitted.\n", session.gpu_name());
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    auto deadline = start + std::chrono::seconds(15);
    auto next_log = start;
    uint32_t frames = 0, valid_head = 0, tracked_head = 0, valid_eyes = 0, renderable = 0;
    XrSessionState previous = XR_SESSION_STATE_UNKNOWN;
    float minimum[3] = {INFINITY, INFINITY, INFINITY};
    float maximum[3] = {-INFINITY, -INFINITY, -INFINITY};
    while (Clock::now() < deadline && session.PollEvents()) {
      if (session.state() != previous) {
        previous = session.state();
        std::printf("Session state: %d\n", int(previous));
      }
      skate3::vr::TrackingFrame frame;
      if (!session.SampleTracking(frame)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      if (!frames) deadline = Clock::now() + std::chrono::seconds(10);
      ++frames;
      renderable += frame.should_render;
      constexpr auto head_valid = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
      constexpr auto head_tracked = XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
      constexpr auto eyes_valid = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
      if ((frame.head.locationFlags & head_valid) == head_valid) {
        ++valid_head;
        const auto& orientation = frame.head.pose.orientation;
        const float norm = orientation.x*orientation.x + orientation.y*orientation.y +
                           orientation.z*orientation.z + orientation.w*orientation.w;
        if (!std::isfinite(norm) || std::abs(norm - 1.0f) > 0.01f)
          throw std::runtime_error("Invalid tracked head quaternion");
        const float position[] = {frame.head.pose.position.x, frame.head.pose.position.y, frame.head.pose.position.z};
        for (int axis = 0; axis < 3; ++axis) {
          if (!std::isfinite(position[axis])) throw std::runtime_error("Non-finite head position");
          minimum[axis] = std::min(minimum[axis], position[axis]);
          maximum[axis] = std::max(maximum[axis], position[axis]);
        }
      }
      tracked_head += (frame.head.locationFlags & head_tracked) == head_tracked;
      valid_eyes += (frame.view_flags & eyes_valid) == eyes_valid;
      if ((frame.view_flags & eyes_valid) == eyes_valid) {
        for (const auto& eye : frame.eyes) {
          if (!(eye.fov.angleLeft < eye.fov.angleRight && eye.fov.angleDown < eye.fov.angleUp))
            throw std::runtime_error("Invalid asymmetric eye FOV");
        }
      }
      if (Clock::now() >= next_log) {
        const auto& p = frame.head.pose.position;
        const auto& q = frame.head.pose.orientation;
        std::printf("Head flags=0x%llx pos=(%.4f %.4f %.4f)m quat=(%.4f %.4f %.4f %.4f) period=%.3fms shouldRender=%d\n",
            static_cast<unsigned long long>(frame.head.locationFlags), p.x, p.y, p.z, q.x, q.y, q.z, q.w,
            double(frame.display_period) / 1e6, int(frame.should_render));
        next_log = Clock::now() + std::chrono::seconds(1);
      }
    }
    session.RequestExit();
    const auto exit_deadline = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < exit_deadline && session.PollEvents() && session.running())
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::printf("Frames=%u valid6DoF=%u tracked6DoF=%u validStereoPoses=%u shouldRender=%u\n",
        frames, valid_head, tracked_head, valid_eyes, renderable);
    if (!valid_head || !tracked_head || !valid_eyes) throw std::runtime_error("No valid tracked 6DoF/stereo samples");
    std::printf("Head position range: (%.4f %.4f %.4f)m\n", maximum[0]-minimum[0], maximum[1]-minimum[1], maximum[2]-minimum[2]);
    std::puts("Vulkan session and predicted 6DoF/eye poses verified. Stereo image rendering remains untested.");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "OpenXR: %s\n", error.what());
    return 1;
  }
}
