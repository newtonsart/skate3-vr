# PCVR development

Work takes place on `vr-development`. Experimental native stereo gameplay now
runs on Quest 3. The user has confirmed that the world fuses and that frontend
menus are visible and the HUD is improved. Complete rear-view visibility across
levels still requires validation. The user also confirmed a complete, readable
pause menu and improved brightness after the color fix below. The standalone
OpenXR probe only checks runtime/session/tracking; it does not render the game.

## Prepare local game data

Install CMake, Ninja, LLVM/Clang 18+, Python 3 and Visual Studio C++ Build Tools
with the Windows SDK. Open an x64 developer command prompt and put Clang and
Ninja on PATH.

Extract a local game ISO into a **new** directory:

```powershell
python cmake/ExtractGameIso.py 'path/to/Skate 3.iso' game
python cmake/test_extract_game_iso.py
```

This supports the XDVDFS partitions recognized by the game's own ISO installer.
It does not read GOD ZIPs. Supply TU3 using `SKATE3_TITLE_UPDATE_PACKAGE` as
described in the main README. Keep all retail data and update packages local.

## OpenXR runtime probe

Use the official Khronos SDK at a fixed revision:

```powershell
git clone --branch release-1.1.54 --depth 1 https://github.com/KhronosGroup/OpenXR-SDK.git out/tools/OpenXR-SDK
cmake -S src/vr -B out/build/openxr -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DSKATE3_OPENXR_SDK_DIR="$PWD/out/tools/OpenXR-SDK"
cmake --build out/build/openxr --target skate3-openxr-probe
./out/build/openxr/skate3-openxr-probe.exe
```

Alternatively enable `SKATE3_BUILD_OPENXR_PROBE` and set
`SKATE3_OPENXR_SDK_DIR` when configuring the game. The loader is built from the
SDK and linked into the probe. Vulkan headers come from the existing ReXGlue
submodule. The probe needs neither retail files nor generated PPC code.

The session diagnostic also needs the Vulkan loader import library. Install
the Vulkan SDK and expose `VULKAN_SDK`, or pass
`-DSKATE3_VULKAN_LIBRARY=C:/VulkanSDK/<version>/Lib/vulkan-1.lib` to CMake.

Set Meta Quest Link as the active OpenXR runtime in the Meta PC app and enter
Quest Link on the headset before running the probe. It reports runtime name,
HMD name, both recommended eye sizes, and the Vulkan API version interval.
A nonzero exit status includes the failing OpenXR operation and result code.
Runtime detection alone does not prove a working Vulkan device/session.

Run `skate3-openxr-probe.exe --track` for a ten-second tracking-only session.
The headset may show an empty background during the test; no image layers are
submitted. The test creates Vulkan 1.3 instance/device objects through OpenXR,
uses the runtime-selected physical device, handles session state transitions,
and performs `xrWaitFrame`/`xrBeginFrame`/`xrEndFrame` with predicted-time
`xrLocateSpace` and `xrLocateViews`. It reports valid/tracked pose counts,
head-position range, display period, and `shouldRender` frames. Non-finite
positions, invalid quaternions/FOVs, or absence of valid 6DoF samples fail the
test. Spaces/session/device/instance are destroyed in dependency order.

On the target Quest 3/RTX 4080, Vulkan 1.3 session creation and 901 valid tracked
6DoF/stereo pose samples passed at an 11.111 ms runtime period (90 Hz). This
measures the tracking loop, not game rendering performance. The runtime reports
1.2 as its highest tested API version; this is not a hard upper limit, per
[Khronos](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrGraphicsRequirementsVulkanKHR.html).

## Renderer integration boundary

`FrameScene` contains the captured world and its row-vector `view_proj` matrix.
The native render thread multiplies object transforms by that matrix and also
uses it for frustum culling. Stereo must render the same immutable scene twice
with per-eye matrices and compatible per-eye culling. Replaying the flat output
texture into OpenXR would not satisfy either VR mode.

The integration creates ReXGlue's Vulkan instance/device through OpenXR and
binds that same device and graphics queue to the XR session. It renders the same
immutable scene for each eye, flushes the native renderer, waits for its writes,
copies to the acquired XR image, and waits before releasing it. This deliberately
serial implementation is a correctness baseline, not an optimized VR renderer.

`cmake/ApplyOpenXRInterop.py` applies the small creation/image/submission hooks
to a disposable copy of the pinned SDK. Set `REXSDK_DIR` to that copy, without a
top-level `.git` entry. The script refuses the source submodule, checks exact
patch anchors and is idempotent. Preserve unrelated local submodule changes.

Configure the game with `SKATE3_ENABLE_OPENXR=ON`, `SKATE3_OPENXR_SDK_DIR`,
`SKATE3_VULKAN_LIBRARY` and `SKATE3_GLSLANG` (the Vulkan SDK's
`glslangValidator.exe`). Launch with `--gpu_backend=vulkan --skate3_vr=true`.
Ctrl+F8 recenters the headset. The original gamepad remains the gameplay input.
Frontend/loading UI uses a view-relative panel. The in-game pause now keeps a
fresh stereo world behind its captured menu, with a flat compatibility option;
this new pause path still needs headset validation. Settings are also composited
into the headset; see [PCVR_SETTINGS.md](PCVR_SETTINGS.md).
Gameplay uses independently rendered eye projections. The optional stereoscopic
window and passthrough modes are described in [PCVR_PASSTHROUGH.md](PCVR_PASSTHROUGH.md).

`skate3_vr_linear_color` defaults to true after isolated headset validation.
Ctrl+F9 toggles conversion for comparison or recovery. The earlier black image
was reproduced with validation: Meta's XR images lacked STORAGE usage despite
the unordered-access request. Conversion now writes into an app-owned Vulkan
storage image and copies that image into the runtime's transfer destination.
The user confirmed restored images and improved brightness. `skate3_vr_capture=true` writes ignored PPM
captures of the source image, before the compositor conversion; these are not
proof of the final image displayed in the headset.

The new-career difficulty transition also hit upstream
[issue #184](https://github.com/mchughalex/skate3recomp/issues/184).
Following the diagnosis in
[932902e](https://github.com/dougieprishpreed/skate3recomp-plus/commit/932902e72463b6402a763337dde4026f07fdbc7a),
early movie-skip presses are retained until 1500 ms of frontend playback.
Boot logos still skip immediately. The user confirmed that the corrected build
passes the difficulty screen. `skate3-movie-skip-test` checks the timing gate;
`skate3-stereo-math-test` checks eye matrices and HUD placement.

## Recovery and validation notes

The attempted replacement of guest culling planes and additional native eye
culling were removed after the user reported missing ground and incorrect NPC
appearance. The user confirmed recovery of both with the HUD change retained.
Do not restore that culling replacement based on synthetic plane tests alone.

A subsequent conservative expansion preserves the original normals and moves
only their distances outward by 200 game units. It matches the last captured
main-camera position against the four perspective side planes, supports either
distance-sign convention, and rejects stale camera snapshots, orthographic
volumes, unrelated cameras and invalid values. Original plane bits are restored
after the scoped call. Tests check that every existing half-space grows.
The user confirmed retained ground/NPC appearance and improved visibility when
looking behind. `skate3_vr_expand_game_frustum` defaults on in VR; Ctrl+F10 toggles
it. This is a finite expansion of loaded geometry, not proof of complete
360-degree visibility across all levels or streaming transitions.

Explicitly enable the installed validation layer during diagnostics:

```powershell
$env:VK_LAYER_PATH = 'C:/VulkanSDK/<version>/Bin'
# Append to the normal game launch:
# --vulkan_validation_enabled=true --vulkan_log_debug_messages=true
```

Check the startup log for `VK_LAYER_KHRONOS_validation`; requesting validation
without an available layer silently produced a run without it on this PC.
Validation found the Meta runtime adding a timeline feature node alongside
ReXGlue's Vulkan 1.2 aggregate feature node. The interop patch uses individual
uniform-layout/scalar-layout/timeline nodes for XR, explicitly requests the
sampler mirror-clamp extension and enables the supported multisample storage
image feature. These device-creation errors disappeared in the recovery run.

Longer gameplay then exposed stale streamed meshes requesting indices beyond
the resident index buffer. The patch now checks both the declared binding range
and buffer allocation before recording indexed draws, with rate-limited rejection
logs. The subsequent hardware run rejected six stale draws without the previous
VUID errors. This prevents invalid reads while decode commits catch up; it does not
claim to repair every underlying streaming mismatch.

The first run without validation failed during Vulkan submission. ReXGlue's
desktop presenter and OpenXR were sharing a queue, but only our Vulkan submits
held its mutex. The session now borrows that same recursive mutex for
`xrBeginFrame`/`xrEndFrame`, and image acquire/release use it too, as required by
[Khronos](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_vulkan_enable-concurrency.html).
`xrWaitFrame` and `xrWaitSwapchainImage` remain outside that lock. The subsequent
non-validation run passed the former failure point and continued into gameplay.
Observed render/copy intervals were often 7–10.4 ms with roughly 1300–2100
items, but submission cadence varied. These CPU intervals include synchronous
GPU waits; they are not separate GPU timestamp measurements or proof of stable
72/90 Hz across levels.

Ctrl+F7 requests a source-image stereo capture when an artifact is visible.
The user reported near-ground spikes only in the headset, increasing with
speed. Quest Link's local diagnostic CLI reported ASW mode `auto`. A temporary
`server:asw.Off` test, together with the synchronization fix above, produced a
user report that the spikes were absent. The prior `auto` mode was restored for
comparison, and the user confirmed no spikes while accelerating and improved
performance. The final runtime setting remains `auto`; disabling ASW is not a
requirement of this checkpoint. This does not isolate ASW as the cause; captures
before the runtime's reprojection cannot show compositor-generated artifacts.
