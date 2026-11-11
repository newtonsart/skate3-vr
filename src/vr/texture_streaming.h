#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace skate3::vr {

struct TextureStreamBudget {
  static constexpr uint64_t kBytes = 2 * 1024 * 1024;
  static constexpr unsigned kResults = 8;
  uint64_t bytes = 0;
  unsigned results = 0;

  bool Take(uint64_t upload_bytes) {
    // Leave an escape hatch for one large result to avoid blocking the queue.
    if (results == kResults ||
        (upload_bytes != 0 && bytes != 0 &&
         upload_bytes > kBytes - std::min(kBytes, bytes))) {
      return false;
    }
    ++results;
    bytes += upload_bytes;
    return true;
  }
};

inline bool CanGrowTextureBlocks(uint64_t frame, uint64_t next_frame, uint64_t upload_bytes) {
  return frame >= next_frame && upload_bytes <= TextureStreamBudget::kBytes;
}

inline std::array<float, 2> PredictStreamPosition(
    std::array<float, 2> previous, std::array<float, 2> current, float seconds) {
  if (!(seconds >= 0.001f && seconds <= 0.15f)) return current;
  const float dx = current[0] - previous[0];
  const float dz = current[1] - previous[1];
  const float speed = std::hypot(dx, dz) / seconds;
  if (!std::isfinite(speed) || speed < 1.0f || speed > 30.0f) return current;
  const float scale = std::min(0.35f, 5.0f / speed) / seconds;
  return {current[0] + dx * scale, current[1] + dz * scale};
}

}  // namespace skate3::vr
