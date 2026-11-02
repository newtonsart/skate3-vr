# PCVR frame-work optimization experiment

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

The optimization **defaults off pending headset validation**. GPU timing and
automatic comparison also default off. The `vr-v0.1.0-alpha` tag is unchanged.

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

For a later headset check, enable:

```text
--skate3_vr_share_frame_work=true
```

Still required: stereo shadow appearance during movement/leaning, loading and
map changes, HUD/menu regression checks, longer sessions, and actual headset
frame-time/latency measurements. The automated stereo/culling tests now also
check timestamp unit conversion, wrap and invalid timestamp parameters.
