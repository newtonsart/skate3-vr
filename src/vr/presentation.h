#pragma once
#include <cstdint>
namespace skate3::vr {
enum class EditorView { Default, Photo, Objects };
inline EditorView SelectEditorView(bool enabled,bool photo,bool object_selected) {
  return !enabled ? EditorView::Default : photo ? EditorView::Photo : object_selected ? EditorView::Objects : EditorView::Default;
}
inline bool CanPresentWorld(bool has_items,int64_t age_ns,bool in_menus,bool pause_in_world,bool seen_gameplay) {
  return has_items && age_ns>=0 && age_ns<=250'000'000 &&
      (!in_menus || (pause_in_world && seen_gameplay));
}
}
