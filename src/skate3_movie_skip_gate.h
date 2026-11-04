#pragma once
#include <algorithm>
#include <cstdint>

// Frontend transitions must observe a playing movie before completion (#184).
class Skate3MovieSkipGate {
 public:
  bool Poll(int64_t now_ms, bool requested, bool frontend, int64_t minimum_ms) {
    if (!initialized_ || now_ms - last_poll_ms_ > 250) {
      start_ms_ = now_ms;
      pending_ = false;
      initialized_ = true;
    }
    last_poll_ms_ = now_ms;
    pending_ = pending_ || requested;
    if (pending_ &&
        (!frontend || now_ms - start_ms_ >= std::max<int64_t>(0, minimum_ms))) {
      pending_ = false;
      return true;
    }
    return false;
  }
 private:
  bool initialized_ = false;
  bool pending_ = false;
  int64_t last_poll_ms_ = 0;
  int64_t start_ms_ = 0;
};
