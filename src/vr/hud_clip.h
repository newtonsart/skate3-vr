#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace skate3::vr {
struct HudClipRect { int32_t left=0,top=0,right=0,bottom=0; };
inline HudClipRect HudPanelClip(uint32_t width,uint32_t height,const float transform[4],float band=1) {
  if(!width || !height || width>INT32_MAX || height>INT32_MAX || !std::isfinite(band) || band<=0) return {};
  for(int i=0;i<4;++i) if(!std::isfinite(transform[i])) return {};
  const double x=transform[2],y=transform[3];
  const double sx=std::abs(double(transform[0]))*band,sy=std::abs(double(transform[1]));
  const auto pixel=[](double ndc,uint32_t size) {
    // A pixel is covered when its centre is inside the original frame.
    const double value=std::clamp((ndc+1)*0.5*size-0.5,0.0,double(size));
    return int32_t(std::ceil(value));
  };
  return {pixel(x-sx,width),pixel(-y-sy,height),pixel(x+sx,width),pixel(-y+sy,height)};
}
}
