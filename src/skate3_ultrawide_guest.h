#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#if SKATE3_HAS_OPENXR
#include <atomic>
#include "vr/cull_frustum.h"
#include <rex/kernel/guest_presence.h>
#include <rex/logging.h>
#endif

#include <rex/cvar.h>
#include <rex/graphics/native_guest_renderer.h>

// Guest-side ultrawide support: the native renderer draws true widescreen
// frames (wide guest output + Hor+ projection), but the game's own CPU
// culling still tests against its 16:9 view frustum and would drop side
// content before it ever reaches the renderer. The generated cull routine
// uses this helper to widen the desktop planes or load the VR expansion
// directly into its local vector registers.

inline bool Skate3UltrawideHasExpandedTargetAspect() {
  if (!rex::cvar::Query<bool>("skate3_ultrawide") ||
      !rex::graphics::IsNativeGuestOutputActive()) {
    return false;
  }

  return rex::cvar::Query<double>("skate3_ultrawide_target_aspect") >
         (16.0 / 9.0) + 0.01;
}

inline bool Skate3UltrawideShouldWidenGameCullFrustum(u8* base, uint32_t cull_block) {
  // Every cull block reaching the vectorized cull job gets its left/right
  // planes widened: DoFrustumCulling reads them unconditionally, including
  // blocks with no extra cull volumes (the word at +5260); gating on that
  // count left 16:9 culling holes at the frame edges in the areas whose
  // blocks carry none. Over-inclusion is safe (a few extra submitted
  // meshes); the plane sanity checks below still veto implausible data.
  (void)base;
  return cull_block != 0 && Skate3UltrawideHasExpandedTargetAspect() &&
         rex::cvar::Query<bool>("skate3_ultrawide_widen_game_frustum");
}

struct Skate3GuestCullPlanes {
  Skate3GuestCullPlanes(PPCContext& ctx_, u8* base_, uint32_t cull_block)
      : ctx(ctx_), base(base_) {
#if SKATE3_HAS_OPENXR
    if ((rex::cvar::Query<bool>("skate3_vr") ||
         rex::cvar::Query<bool>("skate3_vr_compare_frame_work")) &&
        rex::cvar::Query<bool>("skate3_vr_expand_game_frustum") &&
        rex::graphics::IsNativeGuestOutputActive() &&
        rex::kernel::guest_presence::GameplayContextValue() == 1 &&
        cull_block >= 0x10000 && cull_block < 0xFFFFD000) {
      float camera[3];
      if ((cull_block & 15) || !skate3::vr::GetGameCullCamera(camera)) return;
      block = cull_block;
      std::array<float, 24> original;
      for (uint32_t p=0;p<6;++p) {
        const auto plane=LoadPlane(5152+p*16);
        std::copy(plane.begin(),plane.end(),original.begin()+p*4);
      }
      vr_active = skate3::vr::ExpandMainCullFrustum(original, camera, 200.0f, vr_planes);
      static std::atomic<unsigned> applied{0};
      if (vr_active && applied.load(std::memory_order_relaxed) < 4 &&
          applied.fetch_add(1, std::memory_order_relaxed) < 4) {
        REXLOG_INFO("VR: expanded matched main cull block {:08X} camera=({:.2f},{:.2f},{:.2f}) plane0.d={:.2f}->{:.2f}",
                    block, camera[0], camera[1], camera[2], original[3], vr_planes[3]);
      }
      return;
    }
#endif
    if (!Skate3UltrawideShouldWidenGameCullFrustum(base, cull_block)) {
      return;
    }

    const double target_aspect = std::clamp(
        rex::cvar::Query<double>("skate3_ultrawide_target_aspect"), 16.0 / 9.0, 8.0);
    side_scale = float((16.0 / 9.0) / target_aspect);
    if (!(side_scale > 0.0f && side_scale < 0.999f)) {
      return;
    }

    block = cull_block;
    PlaneBits current_left_bits = LoadPlaneBits(kLeftPlaneOffset);
    PlaneBits current_right_bits = LoadPlaneBits(kRightPlaneOffset);

    WidenedPlaneCache& cache = CacheForBlock();
    if (cache.valid && cache.side_scale == side_scale &&
        current_left_bits == cache.widened[0] && current_right_bits == cache.widened[1]) {
      return;
    }

    cache.original[0] = current_left_bits;
    cache.original[1] = current_right_bits;

    Plane left = LoadPlane(kLeftPlaneOffset);
    Plane right = LoadPlane(kRightPlaneOffset);
    Plane widened_left{};
    Plane widened_right{};
    for (size_t i = 0; i < left.size(); ++i) {
      const float center = (left[i] + right[i]) * 0.5f;
      const float side = (left[i] - right[i]) * 0.5f * side_scale;
      widened_left[i] = center + side;
      widened_right[i] = center - side;
    }

    if (!NormalizePlane(widened_left) || !NormalizePlane(widened_right)) {
      return;
    }

    StorePlane(kLeftPlaneOffset, widened_left);
    StorePlane(kRightPlaneOffset, widened_right);
    cache.widened[0] = LoadPlaneBits(kLeftPlaneOffset);
    cache.widened[1] = LoadPlaneBits(kRightPlaneOffset);
    cache.side_scale = side_scale;
    cache.valid = true;
  }

  float VrPlaneDistance(size_t plane, float original) const {
#if SKATE3_HAS_OPENXR
    if (vr_active) return vr_planes[plane * 4 + 3];
#endif
    return original;
  }

 private:
  using Plane = std::array<float, 4>;
  using PlaneBits = std::array<uint32_t, 4>;

  struct WidenedPlaneCache {
    uint32_t block = 0;
    std::array<PlaneBits, 2> original{};
    std::array<PlaneBits, 2> widened{};
    float side_scale = 1.0f;
    bool valid = false;
  };

  static constexpr uint32_t kLeftPlaneOffset = 5152;
  static constexpr uint32_t kRightPlaneOffset = 5168;

  float LoadFloat(uint32_t address) const {
    PPCRegister value{};
    value.u32 = REX_LOAD_U32(address);
    return value.f32;
  }

  void StoreFloat(uint32_t address, float number) const {
    PPCRegister value{};
    value.f32 = number;
    REX_STORE_U32(address, value.u32);
  }

  Plane LoadPlane(uint32_t offset) const {
    Plane plane{};
    for (uint32_t i = 0; i < plane.size(); ++i) {
      plane[i] = LoadFloat(block + offset + i * 4);
    }
    return plane;
  }

  void StorePlane(uint32_t offset, const Plane& plane) const {
    for (uint32_t i = 0; i < plane.size(); ++i) {
      StoreFloat(block + offset + i * 4, plane[i]);
    }
  }

  PlaneBits LoadPlaneBits(uint32_t offset) const {
    PlaneBits plane{};
    for (uint32_t i = 0; i < plane.size(); ++i) {
      plane[i] = REX_LOAD_U32(block + offset + i * 4);
    }
    return plane;
  }

  WidenedPlaneCache& CacheForBlock() {
    static thread_local std::array<WidenedPlaneCache, 16> cache_entries{};
    WidenedPlaneCache* empty = nullptr;
    for (WidenedPlaneCache& entry : cache_entries) {
      if (entry.valid && entry.block == block) {
        return entry;
      }
      if (!entry.valid && !empty) {
        empty = &entry;
      }
    }

    WidenedPlaneCache& entry = empty ? *empty : cache_entries[block % cache_entries.size()];
    entry = {};
    entry.block = block;
    return entry;
  }

  static bool NormalizePlane(Plane& plane) {
    const float length =
        std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
    if (!(length > 0.00001f) || !std::isfinite(length)) {
      return false;
    }

    const float inv_length = 1.0f / length;
    for (float& value : plane) {
      value *= inv_length;
      if (!std::isfinite(value)) {
        return false;
      }
    }
    return true;
  }

  uint32_t block = 0;
  PPCContext& ctx;
  u8* base = nullptr;
  float side_scale = 1.0f;
#if SKATE3_HAS_OPENXR
  // Keep the expansion local to this job. Other jobs can read the same guest planes.
  std::array<float, 24> vr_planes;
  bool vr_active = false;
#endif
};
