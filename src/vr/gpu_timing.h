#pragma once
#include <cmath>
#include <cstdint>

namespace skate3::vr {
inline double GpuDurationMs(uint64_t begin, uint64_t end, uint32_t valid_bits,
                            float period_ns) {
  if (!valid_bits || valid_bits>64 || !std::isfinite(period_ns) || period_ns<=0)
    return -1;
  const uint64_t mask=valid_bits==64 ? UINT64_MAX : (uint64_t(1)<<valid_bits)-1;
  return double((end-begin)&mask)*double(period_ns)/1000000.0;
}
}
