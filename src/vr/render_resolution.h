#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace skate3::vr {
struct RenderExtent { uint32_t width=0,height=0; };
inline RenderExtent EyeRenderExtent(uint32_t width,uint32_t height,double scale) {
  if (!std::isfinite(scale) || scale<0 || scale>1.25)
    throw std::runtime_error("Invalid VR render scale");
  if (scale==0) return {};
  const double w=std::round(width*scale),h=std::round(height*scale);
  if (!width || !height || w<1 || h<1 || w>8192 || h>8192)
    throw std::runtime_error("VR render extent exceeds supported limits");
  return {uint32_t(w),uint32_t(h)};
}
inline float PanelAspect(uint32_t display_width,uint32_t display_height,
                         uint32_t image_width,uint32_t image_height) {
  if (display_width && display_height) return float(display_width)/display_height;
  if (image_width && image_height) return float(image_width)/image_height;
  throw std::runtime_error("Missing VR panel dimensions");
}
}
