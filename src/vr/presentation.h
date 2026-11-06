#pragma once
#include <cstdint>
#include <algorithm>
namespace skate3::vr {
struct FpsPanelLayout {
  uint32_t x=0,y=0,width=0,height=0;
  float aspect=1;
};
inline FpsPanelLayout FpsPanelCrop(uint32_t source_width,uint32_t source_height,
                                  uint32_t texture_width,uint32_t texture_height,
                                  FpsPanelLayout bounds) {
  if (!source_width || !source_height || !texture_width || !texture_height ||
      bounds.x>=source_width || bounds.y>=source_height || !bounds.width || !bounds.height) return {};
  const uint32_t width=std::min(bounds.width,source_width-bounds.x);
  const uint32_t height=std::min(bounds.height,source_height-bounds.y);
  const uint32_t x=uint64_t(bounds.x)*texture_width/source_width;
  const uint32_t y=uint64_t(bounds.y)*texture_height/source_height;
  const uint32_t right=(uint64_t(bounds.x+width)*texture_width+source_width-1)/source_width;
  const uint32_t bottom=(uint64_t(bounds.y+height)*texture_height+source_height-1)/source_height;
  return {x,y,right-x,bottom-y,float(width)/height};
}
enum class EditorView { Default, Photo, Objects };
inline EditorView SelectEditorView(bool enabled,bool photo,bool object_selected) {
  return !enabled ? EditorView::Default : photo ? EditorView::Photo : object_selected ? EditorView::Objects : EditorView::Default;
}
inline bool CanPresentWorld(bool has_items,int64_t age_ns,bool in_menus,bool pause_in_world,bool seen_gameplay) {
  return has_items && age_ns>=0 && age_ns<=250'000'000 &&
      (!in_menus || (pause_in_world && seen_gameplay));
}
}
