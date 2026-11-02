#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace skate3::vr {
bool GetGameCullCamera(float camera[3]);

// Match the main perspective camera before expanding its original half-spaces.
// Both n.p+d and n.p-d plane conventions occur in game engines.
inline bool ExpandMainCullFrustum(const std::array<float,24>& original,
                                 const float camera[3], float radius,
                                 std::array<float,24>& expanded) {
  if (!std::isfinite(radius) || radius <= 0) return false;
  for (int k=0;k<3;++k) if (!std::isfinite(camera[k])) return false;
  float lengths[6], dots[6];
  for (int p=0;p<6;++p) {
    const float* plane=original.data()+p*4;
    for (int k=0;k<4;++k) if (!std::isfinite(plane[k])) return false;
    lengths[p]=std::hypot(plane[0],plane[1],plane[2]);
    if (lengths[p]<0.001f || !std::isfinite(lengths[p])) return false;
    dots[p]=plane[0]*camera[0]+plane[1]*camera[1]+plane[2]*camera[2];
    if (!std::isfinite(dots[p])) return false;
  }
  for (int p : {0,2}) {
    float opposite_dot=0;
    for (int k=0;k<3;++k) opposite_dot+=original[p*4+k]*original[(p+1)*4+k];
    if (opposite_dot/(lengths[p]*lengths[p+1])<-0.999f) return false;
  }
  float errors[2]{};
  for (int convention=0;convention<2;++convention) {
    const float sign=convention ? -1.0f : 1.0f;
    for (int p=0;p<4;++p)
      errors[convention]=std::max(errors[convention],
          std::abs(dots[p]+sign*original[p*4+3])/lengths[p]);
  }
  const float sign=errors[0]<=errors[1] ? 1.0f : -1.0f;
  if (std::min(errors[0],errors[1])>2.0f) return false;
  float near_far_dot=0;
  for (int k=0;k<3;++k) near_far_dot+=original[16+k]*original[20+k];
  if (near_far_dot/(lengths[4]*lengths[5])>-0.9f) return false;
  const float far_value=(dots[5]+sign*original[23])/lengths[5];
  if (!std::isfinite(far_value) || std::abs(far_value)<2.0f) return false;
  const float direction=far_value>0 ? sign : -sign;
  auto result=original;
  for (int p=0;p<6;++p) {
    result[p*4+3]+=direction*radius*lengths[p];
    if (!std::isfinite(result[p*4+3])) return false;
  }
  expanded=result;
  return true;
}
}
