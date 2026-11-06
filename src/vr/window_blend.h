#pragma once
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace skate3::vr {
struct ColorParameters {
  uint32_t decode_color=1;
  float feather=0,aspect=1;
  uint32_t premultiplied=0;
  float pointer_u=0.5f,pointer_v=0.5f;
  uint32_t pointer_mode=0;
  float pointer_padding=0;
};
static_assert(sizeof(ColorParameters)==32);
inline float WindowFeather(int mode,bool room_active,double amount) {
  if (!std::isfinite(amount) || amount<0 || amount>0.15)
    throw std::runtime_error("Invalid VR window feather width");
  return mode==2 && room_active ? float(amount) : 0.f;
}
}
