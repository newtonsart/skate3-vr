#pragma once
#include <cstdint>
namespace skate3::vr {
inline bool CanPresentWorld(bool has_items,int64_t age_ns,bool in_menus,bool pause_in_world,bool seen_gameplay) {
  return has_items && age_ns>=0 && age_ns<=250'000'000 &&
      (!in_menus || (pause_in_world && seen_gameplay));
}
}
