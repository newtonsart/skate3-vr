# PCVR shared frame-work optimization

## Gameplay trace

The VR launcher enables lightweight diagnostics in the selected save profile's
`skate3.log`. During stereo gameplay, `VR trace:` summarizes 120 submitted
frames: the OpenXR display period, mean and maximum CPU submission interval,
intervals over 1.5 times that period, repeated scene generations, cumulative
game-camera yaw, and the maximum CPU render, transfer/wait and GPU render
times, plus the maximum time spent polling XR events and beginning a frame.
`native-scene perf:` provides the game-thread and resource-work breakdown.
`VR trace worst interval:` records the scene, camera yaw change and timings for
the submission with the largest interval in that window. The interval spans
two submissions and is not necessarily caused by work in the latter frame.
The trace includes
timing queries but adds no new GPU completion waits; it may still add overhead.

Submission intervals do not measure displayed frames, reprojection or headset
latency. GPU timing may be unavailable, indicated by `GPU-samples=0` and a
maximum of -1. Camera yaw is inferred from the game projection, not head
movement, and can be unavailable for unusual projections. These records contain
performance and scene statistics, not captured images or audio. Launching
directly without the VR launcher leaves tracing disabled by default.

2026-11-02, RTX 4080, Windows RelWithDebInfo Vulkan build.

## Changes

`skate3_vr_share_frame_work` allows both eyes to use one mutable render copy of
the immutable captured scene. Only the camera matrices/position change between
eyes; the original published scene is untouched. The left-eye render/copy is
completed before the right-eye camera is written, using the existing GPU waits.

The same per-pair context reuses the light-space shadow atlas, static sun map
and world-shadow updates. These passes use the original game-camera scene;
view-dependent geometry, projection and HUD are still rendered separately.
The context is local to one stereo pair, so it cannot reuse a previous frame's
shadow readiness after a load or a skipped first-eye shadow pass.

The optimization now **defaults on following the user's positive headset test
and the automated headset checks below**. GPU timing and automatic comparison
remain off by default. `--skate3_vr_share_frame_work=false` restores the separate
paths for troubleshooting. The `vr-v0.1.0-alpha` tag is unchanged.

## Measurements

Quest Link remained connected, but the user could not wear the headset. The
diagnostic therefore rendered two identical desktop-camera views of the actual
game scene. It alternated the separate/shared paths on consecutive frames,
after scene generation 300, for 300 samples each. Retail data and a separate
`out/runtime-perf` user-data profile were used locally.

Both variants averaged approximately 1823 scene items in the run without Vulkan
validation (1822.78 separate / 1822.80 shared).

| Measured work per pair | Separate | Shared | Reduction |
| --- | ---: | ---: | ---: |
| CPU render recording, scene copy and copy destruction | 3.736 ms | 3.240 ms | 13.3% |
| CPU scene-copy construction | 0.661 ms | 0.329 ms | 50.3% |
| GPU native rendering | 1.756 ms | 1.702 ms | 3.0% |

An earlier 600-sample validation-enabled run of both changes measured
4.207 -> 3.756 ms CPU rendering and 1.761 -> 1.706 ms GPU rendering, with no
VUID messages. A shadow-only comparison showed a smaller CPU benefit and
approximately the same 3% GPU benefit.

The GPU results come from Vulkan timestamp queries inserted around the native
render commands. The calculation handles timestamp counter wrap and the queue's
valid-bit count. Timestamp retrieval uses the GPU completion waits already
present in the renderer; profiling adds no new completion wait.

These results measure the selected render work, **not overall FPS, latency or
performance in the headset**. The desktop comparison excludes OpenXR transfer
and composition. Its duplicate camera does not test asymmetric eye projections,
binocular shadow consistency or positional head movement. Game logic, streaming,
the desktop presenter and XR frame pacing can still limit the application.

## Reproduction and controls

Build with the existing OpenXR configuration and disposable SDK patch described
in [PCVR_BUILD.md](PCVR_BUILD.md). Append to a normal VR launch:

```text
--skate3_vr_gpu_timing=true
```

This logs GPU render/transfer time for each eye in `VR GPU:` records.
`CPU-render` measures command recording, while the older `GPU-copy/wait` field
includes CPU submission and waits and must not be treated as a GPU timestamp.

For 600 alternating comparison samples:

```text
--skate3_vr_compare_frame_work=true
```

The comparison enables timestamp queries automatically. `VR perf sample:`
records label the source `display=headset` or `display=desktop`; do not merge
these workloads. The desktop path activates only when XR cannot render and
compares duplicate game-camera views without submitting them to the headset.
After 600 valid samples, comparison stops. The isolated local test used the
project's existing `--skate3_demo_path=true` boot automation.

To explicitly enable the optimized path:

```text
--skate3_vr_share_frame_work=true
```

The user subsequently ran this command and reported that the optimized game
worked very well in the headset. The recorded session confirmed the option was
enabled, exceeded 18,000 stereo submissions and contained no submission failure
messages. Across 139 logged gameplay timing samples, the render/copy interval's
median was 8.84 ms and p95 was 9.74 ms. These sampled intervals exclude some game
and runtime work and are not full frame-time or latency measurements.

A follow-up automated comparison used actual OpenXR eye views and swapchains,
with the connected headset left stationary. All 600 comparison samples were
labelled `display=headset`; none used the desktop fallback. The validation run
contained no VUID messages or Vulkan submission failures. A scripted pause/back
sequence also executed during the test. This supplements, but does not replace,
the user's visual check.

| Headset-connected validation run, per pair | Separate | Shared | Reduction |
| --- | ---: | ---: | ---: |
| CPU render recording and scene-copy lifetime | 4.257 ms | 3.772 ms | 11.4% |
| GPU native rendering | 1.535 ms | 1.471 ms | 4.1% |
| GPU transfer to OpenXR | 0.126 ms | 0.128 ms | Within measurement variation |

Each variant averaged approximately 1674 scene items. The validation layer
substantially slowed command replay and overall submission cadence, so these
figures are comparisons of the recorded regions, not headset FPS claims.

The next run without validation completed a further 600 samples, but the
runtime had stopped marking the stationary headset renderable: all those
samples correctly reported `display=desktop`. They measured 3.875 -> 3.357 ms
CPU recording and 1.763 -> 1.695 ms GPU rendering. They are not counted as
headset measurements. The scripted map-selection sequence executed in the
isolated profile, but the inspected log did not unambiguously confirm a new
map load; that coverage remains pending.

Still required: broader movement/leaning and map coverage, longer sessions, and
end-to-end headset frame-time/latency measurements. The automated
stereo/culling tests also check timestamp unit conversion, wrap and invalid
timestamp parameters.
