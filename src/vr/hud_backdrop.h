#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

namespace skate3::vr {
struct HudQuad {
  uint32_t flags=0,source_stride=0,width=0,height=0;
  bool valid=false,black=false;
  float left=0,right=0,bottom=0,top=0;
};
inline HudQuad DescribeHudQuad(uint32_t flags,uint32_t source_stride,uint32_t fetch_size,const float* c,
                               std::span<const uint8_t> vertices,uint32_t count,uint32_t stride) {
  HudQuad quad; quad.flags=flags; quad.source_stride=source_stride;
  quad.width=(fetch_size&8191)+1; quad.height=((fetch_size>>13)&8191)+1;
  if((source_stride!=16 && source_stride!=24) || stride!=28 || (count!=4 && count!=6) || count>vertices.size()/stride ||
      c[12]!=0 || c[13]!=0 || c[14]!=0 || c[15]!=1) return quad;
  const bool alpha=std::isfinite(c[35]) && c[35]>=0 && c[35]<=1.01f;
  quad.black=c[32]==0 && c[33]==0 && c[34]==0 && alpha;
  quad.left=quad.bottom=1e9f; quad.right=quad.top=-1e9f;
  for(uint32_t i=0;i<count;++i) {
    const auto* vertex=vertices.data()+i*stride;
    float p[4],world[4]{},clip[4]{}; std::memcpy(p,vertex,sizeof(p));
    for(int r=0;r<4;++r) for(int k=0;k<4;++k) world[r]+=p[k]*c[16+r*4+k];
    for(int r=0;r<4;++r) for(int k=0;k<4;++k) clip[r]+=world[k]*c[r*4+k];
    if(!std::isfinite(clip[3]) || std::abs(clip[3])<1e-5f) return quad;
    const float x=clip[0]/clip[3],y=clip[1]/clip[3];
    if(!std::isfinite(x) || !std::isfinite(y)) return quad;
    quad.left=std::min(quad.left,x); quad.right=std::max(quad.right,x);
    quad.bottom=std::min(quad.bottom,y); quad.top=std::max(quad.top,y);
  }
  quad.valid=true; return quad;
}
inline bool FullscreenHudBackdrop(const HudQuad& quad) {
  return quad.valid && quad.source_stride==16 && quad.width==1 && quad.height==1 &&
      quad.right-quad.left>=1.9f && quad.top-quad.bottom>=1.9f;
}
inline int LoadingBackdrop(std::span<const HudQuad> draws) {
  // Captured loader: SimpleDraw fill, then small APT spinner tiles. Match all fade phases.
  for(size_t i=0;i<draws.size();++i) {
    const auto& fill=draws[i]; const auto tail=draws.size()-i-1;
    if(!FullscreenHudBackdrop(fill) || !fill.black || fill.flags!=0x29 || tail<3 || tail>8) continue;
    bool spinner=true;
    for(size_t j=i+1;j<draws.size();++j) {
      const auto& tile=draws[j];
      spinner &= tile.valid && tile.flags==0x0d && tile.source_stride==24 &&
          tile.width<=64 && tile.height<=64 && tile.left>0.5f && tile.top< -0.4f &&
          tile.right-tile.left<0.3f && tile.top-tile.bottom<0.3f;
    }
    if(spinner) return int(i);
  }
  return -1;
}
}
