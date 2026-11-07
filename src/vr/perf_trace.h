#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace skate3::vr {
struct XrFrameTiming {
  double poll=0, wait=0, begin=0, tracking=0, end=0, other=0;
};
struct PerfTrace {
  XrFrameTiming worst_xr;
  uint32_t frames=0, intervals=0, slow=0, repeated=0, gpu_samples=0;
  double interval_sum=0, interval_max=0, render_max=0, wait_max=0, xr_wait_max=0, gpu_max=0;
  double yaw_travel=0, last_yaw=0;
  double worst_render=0, worst_wait=0, worst_xr_wait=0, worst_gpu=-1, worst_yaw_delta=-1;
  uint64_t worst_scene=0;
  uint32_t worst_items=0;
  uint64_t last_scene=0;
  bool have_scene=false, have_yaw=false;

  void Add(double interval, double period, double render, double wait, double xr_wait, double gpu,
           uint64_t scene, const float* vp, const float* proj, XrFrameTiming xr = {}, uint32_t items = 0) {
    ++frames;
    const bool worst=interval>interval_max;
    if (interval>0) {
      ++intervals;
      interval_sum+=interval;
      interval_max=std::max(interval_max,interval);
      if (period>0 && interval>period*1.5) ++slow;
    }
    render_max=std::max(render_max,render);
    wait_max=std::max(wait_max,wait);
    xr_wait_max=std::max(xr_wait_max,xr_wait);
    if (gpu>=0) { ++gpu_samples; gpu_max=std::max(gpu_max,gpu); }
    if (have_scene && scene==last_scene) ++repeated;
    last_scene=scene; have_scene=true;
    double yaw_delta=-1;
    if (std::isfinite(proj[11]) && std::abs(proj[11])>1e-6 &&
        std::isfinite(vp[3]) && std::isfinite(vp[11]) &&
        std::hypot(vp[3],vp[11])>1e-6) {
      const double yaw=std::atan2(vp[3]/proj[11],vp[11]/proj[11]);
      if (have_yaw) { yaw_delta=std::abs(std::remainder(yaw-last_yaw,2*3.14159265358979323846)); yaw_travel+=yaw_delta; }
      last_yaw=yaw; have_yaw=true;
    } else have_yaw=false;
    if (worst) { worst_xr=xr; worst_items=items; worst_render=render; worst_wait=wait; worst_xr_wait=xr_wait; worst_gpu=gpu; worst_yaw_delta=yaw_delta; worst_scene=scene; }
  }
  void ResetWindow() {
    worst_xr={}; worst_items=0;
    frames=intervals=slow=repeated=gpu_samples=0;
    interval_sum=interval_max=render_max=wait_max=xr_wait_max=gpu_max=yaw_travel=0;
    worst_render=worst_wait=worst_xr_wait=0; worst_gpu=worst_yaw_delta=-1; worst_scene=0;
  }
};
}
