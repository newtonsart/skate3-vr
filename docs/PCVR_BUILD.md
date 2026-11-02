# PCVR development

Work takes place on `vr-development`. No playable VR integration is available
yet. The OpenXR probe checks the actual installed runtime and can verify a
Vulkan session with predicted head/eye tracking. It does not render the game.

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

The next gameplay integration must use the runtime-selected Vulkan physical device,
graphics requirements, and device creation path before creating an XR session.
The standalone session implements events, predicted display time, reference
spaces and `xrLocateViews`. Gameplay still needs shared device creation,
acquired swapchain images and GPU completion before release. The existing
Vulkan device creation and native-output hooks need extensions before the
module can safely present the game. Its RHI currently exposes no public image
import, Vulkan device binding, or submission-completion hook to the app-side
renderer. Calling `RenderScene` twice without accounting for those boundaries
would reuse mutable depth/postprocessing state and break image ownership.
