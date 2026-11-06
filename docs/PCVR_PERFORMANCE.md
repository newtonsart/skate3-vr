# PCVR shared frame-work optimization

## Automatic frame pacing in VR

With `skate3_guest_fps_cap_auto=true`, VR leaves the guest loop uncapped by the
desktop monitor so the existing OpenXR frame waits pace submission. Desktop
runs still use the monitor-derived VRR limit. An explicit guest limit remains
available with Auto off. No saved settings are rewritten by this change.

The November 4 gameplay trace ran near 71 submissions/s against a 90 Hz OpenXR
period, with about 27% of intervals longer than 1.5 display periods, including
camera-stationary windows. The old Auto path always consulted the desktop
monitor. Removing that second clock addresses the conflicting limit; hardware
measurements must still establish the resulting cadence and visual smoothness.

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


## Consecutive render/transfer waits (2026-11-04)

The user's 20:07:26–20:09:59 session requests an 8.33 ms OpenXR period
(120 Hz), but contains sustained 16.67 ms submission intervals. It also
contains genuine 8.33 ms intervals, so this is not a universal 60 FPS cap.
Busy intervals have native GPU draw times near 1 ms while CPU recording,
guest capture/build and transfer waits consume much more of the budget.

Each eye previously blocked the CPU on the native-render submission and
then blocked again on the immediately following XR transfer. The XR path
now submits the render without the first wait. Both submissions use graphics
queue 0; the existing barriers and final transfer fence cover both before
releasing the image to OpenXR or reusing the source for the other eye.
The synchronous helper remains the default for callers such as the GPU
benchmark. UI-only transfers no longer flush an empty native submission.

The FPS panel also uses the actual ImGui widget bounds instead of a fixed
320x200 crop. Its clear is transparent and the XR layer uses premultiplied
alpha. Encoded premultiplied RGB is unpremultiplied before linear decoding
and multiplied again afterward. Full settings panels retain their background.
The existing test mode captures only the FPS widget when `show_fps_counter`
is enabled, otherwise it opens the settings panel for inspection.

Validation records and timing limitations are in the outer workspace's
`out/vr-fps-queue-validation-20261104.md`. A higher submission rate in an idle
scene does not establish sustained 120 FPS while moving through busy areas.

## Static meshes outside the VR eye view (2026-11-04)

VR draws now skip static meshes outside the current eye frustum with a 20%
side margin. Skinned, cloth and ROPA meshes are always drawn. A static mesh is
eligible only after decode checked every vertex against its captured bounding
box; absent, changed or inaccurate bounds fail open. Shadow rendering is
unchanged. The switch `skate3_vr_static_frustum_cull` can turn this off.

An isolated headset-connected run compared the same scene with the switch off
and on, using the same eye view at -60, -30, 0, 30 and 60 degrees. Thirteen
captures compared 7,327,200 pixels each; none differed by more than one 8-bit
color level. One captured scene is not a proof for all maps, transparency,
animated shaders, real head movement or every streaming transition.

For scene generations 600-2800, the automated baseline's 19 timing windows
had median 86.81 submissions/s, 1481 items and 4.58 ms maximum CPU recording.
The protected-culling run's 19 windows had median 117.92 submissions/s,
1840 items and 3.36 ms maximum CPU recording; 15 reached 115 submissions/s.
Scene content and animation were not identical, so the comparison is
indicative, not a controlled benchmark or a guarantee of sustained 120 FPS.
Recorded details are in `out/runtime-vr-cull-safe-perf/skate3.log` and
`out/runtime-vr-cull-pixel-diff/skate3.log` in the outer workspace.

## Reuse the shared scene-copy storage (2026-11-06)

Immersive user logs still contain long 60/s intervals. A complete FrameScene
was constructed and destroyed for each stereo pair; DrawItem's draws and bones
vectors caused nested allocations even though each pair finishes synchronously.
State now owns reusable copy storage. Copy assignment refreshes all scene data
while retaining vector capacities. The source remains independent and each
eye still supplies its own camera. Both desktop comparison and XR rendering
call the same copy method. The existing shared-work switch remains in use.

The focused harness compiles that method with the real FrameScene. It checks
source isolation, camera restoration, nested data, allocation reuse, and empty,
growing and shrinking item counts. A 2048-item synthetic fixture with three
draws and 48 bone floats per item measured 0.5442 ms for constructing/freeing
versus 0.0799 ms for reusing storage. This does not establish an FPS gain.

Build and desktop gameplay startup succeeded. Live OpenXR validation was
unavailable: the probe returned XR_ERROR_FORM_FACTOR_UNAVAILABLE (-35) on
2026-11-06; both the installed and candidate game failed at VR initialization.
The new executable is staged at the outer workspace's
`out/immersive-copy-candidate-20261106/Skate3-VR.cmd`; the usual launcher retains
the previous build. The user's saved settings and save were not changed by QA.

## No-headset eye rendering and moving-player test (2026-11-06)

`skate3_vr_compare_frame_work=true` with `skate3_vr=false` now runs the existing
alternating shared/separate benchmark without initializing OpenXR. The timing
query setup is shared with the normal XR path. Samples are explicitly labelled
`display=simulated`; normal XR fallback samples remain `display=desktop`.
The simulated views use recorded asymmetric Quest FOV values and nominal 64 mm
IPD. The main-camera capture frustum is expanded as in VR, so the test does not
artificially limit geometry to the desktop camera. The existing
`skate3_vr_resolution_test=true` diagnostic supplies a 2840x3080 render target at
scale 1.25 (an older recorded headset recommendation, larger than the last real
session's 2580x2840). These tests do not create XR swapchains or measure the
color/transfer passes, compositor, Meta Link, reprojection or headset latency.

`skate3_vr_simulate_route=true` extends that test to 90 seconds and drives the
existing controller driver: repeated pushes, then left/right steering. It starts
only after gameplay is active; menus release the controls, the route releases
them at completion, and the driver's existing stale-sample timeout remains.
The flag is off in normal play. The deterministic control schedule is not a
pathfinding system: collisions can stop the skater. Logs record camera and
player-bone positions once per second so actual travel can be checked rather
than inferred from issued controls. No SDK input injection patch was needed.

The isolated test used a copy of the user's save and settings, including 2x
resolution and 100 m pre-streaming. The run recorded 90 position samples,
about 486 m of cumulative bone-position travel and 69 seconds with >1 m of
position change. There were 12412 benchmark samples and no error-level messages.
A later full-text audit found 10 texture decode failures at info level and
three failed file opens at warning level; this was not a failure-free load.
During moving seconds, the shared path's pair wall time had median 4.1296 ms,
p95 5.5849 ms and max 19.4148 ms, with 3/4812 pairs over 8.33 ms. Separate work
had median 4.8842 ms, p95 6.5524 ms and 7/4814 pairs over 8.33 ms. Both variants
had median 1438 objects and maximum 2160. Pair times include native recording
and blocking GPU submission completion, but not guest frame capture/build or
XR delivery. They cannot be converted directly to headset FPS.

The focused stereo test covers the route boundaries, turns, button pulses,
nonfinite times and release. `test_vr_scene_copy.py` covers the scene storage
reuse. Results and raw logs are in the outer workspace:
`out/simulated-route-results-20261106.json` and
`out/runtime-simulated-route-20261106/skate3.log`.

## Snapshot texture routes once per stereo frame (2026-11-06)

The main draw pass previously double-read each live texture object's fetch
words for every material use, including repeated surfaces and the second eye.
TexRoute now records its refresh frame. Both eyes use the same frame number,
so they share one stable snapshot and the next frame observes streaming
updates. Retained items still perform no live reads. This does not freeze
payload revalidation or discard scene objects.

The stable reader also rejects non-texture fetch types and formats unsupported
by the existing decoder, left in recycled object memory. On an unreadable,
unstable or invalid snapshot, the previous route
is held and payload polling is suspended until live words return; probing the
old allocation could otherwise replace good cached art with recycled bytes.
Prewarm and settle paths skip snapshots with no base address, including those
whose low format bits are nonzero. Decode failures remain logged.

`cmake/test_texture_routes.py` compiles the actual reader and route refresh
block. It covers 10000 shared uses with one double-read, next-frame updates,
demote/re-promote, freed and unreadable objects, unstable snapshots, retained
items and recovery after an initial failure. The output is a read-count and
correctness check, not a whole-game frame-rate benchmark.

The final isolated 90-second run completed 12367 samples, with no logged texture
decode failures, error-level messages or access violations; the three failed
file opens remained. The intermediate run reproduced four pending FFmpeg XMA
errors and four unsupported-format texture failures. Their absence in the
final run does not prove the audio fault fixed. Texture-stage cost averaged
0.253 versus 0.203 us per item, but moving shared-pair medians stayed essentially
unchanged (3.1654 versus 3.1952 ms). Routes diverged due to collisions: 117 m
versus 126 m estimated travel, with 33 moving intervals each. No global FPS
gain or sustained headset rate is established. Details are in the outer
workspace's `out/texture-routes-results-20261106.md` and `.json`. Only the
separate candidate was updated; the usual launcher's binary is unchanged.

## Eliminate the per-frame submitted set in static retention (2026-11-06)

The off-screen retention pass rebuilt an unordered_set of all submitted static
keys each guest frame. Every resident submitted entry already receives
last_seen=g_guest_frame before the sweep, so the sweep can test that stamp
directly. New keys rejected by the growth cap have no resident entry to sweep.
This removes temporary nodes and membership hashing with the same visibility,
TTL, identity and core-refresh rules.

`cmake/test_static_retention.py` compiles the real pass, scene types, key and
frustum functions. It compares the old algorithm across 400 frames and covers
turns, removals, lifetime boundaries, dynamic exclusions, duplicates, cache
clears and the growth cap. A controlled 2048-item fixture measured 0.1047 ms
versus 0.0354 ms and 2051 versus zero allocations per frame in this pass.

A 90-second simulated-eye traversal completed 11335 samples and approximately
656 m of player-position travel, with 76 moving intervals. Median per-window
retention averages were 0.32 ms before and 0.21 ms after; routes differed.
There were no texture decode failures or access violations, but eight pending
XMA errors and three failed file opens remained. Moving shared-pair median/p95
were 4.94/7.04 ms, with 18/4703 pairs over 8.33 ms and a 35.14 ms maximum.
The worst pair spent only 3.78 ms in measured CPU recording and 1.12 ms in GPU
drawing, pointing to remaining waits/host scheduling outside those measures.
No global FPS gain or sustained headset rate is established. See the outer
workspace's `out/static-retention-results-20261106.md` and `.json`. Only the
separate candidate was updated; the prior candidate is in before-static-retention.

## Attribute waits and submit native rendering before XR image acquisition (2026-11-06)

The simulated benchmark now logs CPU-begin, CPU-flush and CPU-query alongside
pair-total. All 11614 samples passed a CPU-stage accounting check. Shared-path
medians were 0.0517/2.2272/0.0021 ms for those stages and 4.6389 ms for the pair.
CPU-flush includes deferred command playback, submit, GPU completion and
submission restart, not just idle time. The earlier 35 ms spike was not
reproduced. The no-headset benchmark still waits between eyes; real XR already
used its transfer completion fence to cover native rendering on the same queue.

Transfer now submits rendering to the application's own image before acquiring
and waiting for the compositor-owned XR image, allowing that rendering to
overlap image availability. XR image writes remain after successful wait and
the transfer completion fence remains before release/resource reuse. On a
failure after early submit, pending native work is drained before mono fallback.
Settings-only transfers still skip native submission. Periodic VR transfer CPU
logs split native submit, XR image acquire/wait, and completion per eye.

`cmake/test_vr_transfer_order.py` compiles the actual prefix, completion and
cleanup blocks against API doubles, covering normal/UI ordering and failure
paths, including cleanup exceptions. Build, interop and stereo tests passed.
The runtime probe still reports XR_ERROR_FORM_FACTOR_UNAVAILABLE (-35), so the
new XR overlap is not validated on a headset or quantified. Its benefit can be
zero if XR images are already available. Further batching would require
separating resources currently rewritten/reused between eyes; no completion
fence was removed. See the outer workspace's `out/wait-profile-results-20261106.md`
and `.json`. Only the separate candidate is updated; its prior build is kept
in before-wait-overlap. XMA errors remain unresolved.
