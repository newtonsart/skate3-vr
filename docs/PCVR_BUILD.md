# PCVR development

Work takes place on `vr-development`. No playable VR integration is available
yet. The OpenXR probe checks the actual installed runtime; it does not render
the game or validate tracking.

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

Set Meta Quest Link as the active OpenXR runtime in the Meta PC app and enter
Quest Link on the headset before running the probe. It reports runtime name,
HMD name, both recommended eye sizes, and the Vulkan API version interval.
A nonzero exit status includes the failing OpenXR operation and result code.
Runtime detection alone does not prove a working Vulkan device/session.

## Renderer integration boundary

`FrameScene` contains the captured world and its row-vector `view_proj` matrix.
The native render thread multiplies object transforms by that matrix and also
uses it for frustum culling. Stereo must render the same immutable scene twice
with per-eye matrices and compatible per-eye culling. Replaying the flat output
texture into OpenXR would not satisfy either VR mode.

The next integration must use the runtime-selected Vulkan physical device,
graphics requirements, and device creation path before creating an XR session.
It must then implement session events, predicted display time, reference spaces,
`xrLocateViews`, acquired swapchain images and GPU completion before release.
The existing Vulkan device creation and native-output hooks need to be extended
before the module can safely present the game.
