<picture>
  <source media="(prefers-color-scheme: dark)" srcset="banner.png">
  <source media="(prefers-color-scheme: light)" srcset="banner-light.png">
  <img alt="Skate3Recomp VR — native OpenXR and Vulkan" src="banner-light.png">
</picture>

# Skate3Recomp VR

An experimental PCVR fork of [Skate3Recomp](https://github.com/mchughalex/skate3recomp),
with native **OpenXR + Vulkan** integration. The game renders a different world
view for each eye and uses headset tracking while retaining the original
gamepad controls. VR development is included in this fork's **`main` branch**.

**Status: playable alpha, with more testing needed.** Immersive gameplay has
been tested on **Windows, Meta Quest 3 over wired Quest Link, and an RTX 4080**.
The user confirmed stereo fusion, improved HUD/menu readability and a large
clarity improvement with headset-sized rendering. This is not a finished VR
release or a guarantee of stable 72/90 FPS across the game.

The project does not include Skate 3 retail game files. To run or build the project, you must provide files from your own legally obtained Xbox 360 copy of Skate 3.

## VR features and current limits

- Native per-eye scene rendering through a shared ReXGlue/OpenXR Vulkan device.
- Third-person immersive view with independent head orientation and positional
  tracking; adjustable camera distance, height and world scale.
- Settings visible inside the headset, navigable with the original controller:
  VR mode, render resolution, camera, window placement, HUD and recentering.
- Headset-sized render targets, independent of the desktop resolution. **HMD
  100%** removed the previous 1440-row upscale in the tested configuration.
- Shared scene/shadow work between eyes, corrected compositor color and queue
  synchronization, plus a fix for the difficulty-selection movie transition.
- In-game pause can retain the stereo world and head tracking behind the menu;
  the previous flat panel remains available for compatibility. This newer pause
  path still needs broader headset testing.

| Mode | Behavior | Validation |
| --- | --- | --- |
| Immersive VR | Stereo world around the viewer, following the skater | Gameplay and image-quality checks on Quest 3 |
| 3D window | Fixed virtual screen with separate eye perspectives and positional parallax | Implemented; broader visual checks pending |
| 3D window + room view | Stereo window over runtime-provided passthrough | Requires optional `XR_FB_passthrough`; camera background not yet validated on the target setup |

OpenXR is not specific to Meta, but **other headsets/runtimes have not been
validated here**. Passthrough support is separate from ordinary OpenXR support.
VR on Linux/macOS is untested; their desktop support comes from the base project.
There is no Touch/motion-controller gameplay. Large head movements, streaming,
other maps and editor/photo/replay flows still need coverage; conservative
culling expansion cannot restore geometry the game has not loaded.

## Run in VR

Use an **OpenXR-enabled build of this fork**. The ordinary desktop build
instructions below do not enable OpenXR. See [PCVR build instructions](docs/PCVR_BUILD.md#build-the-openxr-game-on-windows)
for the Windows toolchain, local game data/TU3, OpenXR SDK and disposable
ReXGlue patch setup.

1. Connect the headset and activate its PCVR session. On Quest, enter Quest Link
   and select Meta Quest Link as the active OpenXR runtime in the Meta PC app.
2. Connect a conventional Xbox/PlayStation controller.
3. From the repository root, launch the configured VR build:

```powershell
./out/build/relwithdebinfo/skate3.exe --gpu_backend=vulkan --skate3_vr=true --game_data_root="$PWD/game" --user_data_root="$PWD/out/runtime-test" --skate3_native_render_scene_msaa=1 --skate3_vr_share_frame_work=true
```

Keep using the same `user_data_root` to retain your saves, profile and settings.
Use your existing path if it differs. VR is opt-in (`skate3_vr` defaults off).

Open **RB + Start → Video → VR Render Resolution** and compare **HMD 100%**.
The build defaults to **Desktop (compatibility)**; the chosen setting is saved.
With the tested runtime recommendation, source sizes are:

| Setting | Per-eye source size | Use |
| --- | --- | --- |
| Desktop (compatibility) | Follows desktop render settings; previously 2560×1440 | Previous rendering path |
| HMD 75% | 1704×1848 | Lower GPU cost |
| HMD 100% | 2272×2464 | First setting to compare for improved clarity |
| HMD 125% | 2840×3080 | More supersampling and GPU cost |

Actual recommendations vary with the runtime/headset settings. HMD 100% draws
about 52% more source pixels than the previous 2560×1440 path on this setup;
that is not a measured FPS cost. Automatic shadows retain their previous
resolution, and retail textures remain unchanged. Start with MSAA off (one
sample) as in the tested launch above. More detail: [image quality](docs/PCVR_IMAGE_QUALITY.md).

### VR controls

| Input | Action |
| --- | --- |
| Original gamepad controls | Play Skate 3 |
| Start | Original pause menu, with the stereo-world route enabled by default |
| RB + Start | Mod settings, also visible in the headset; configurable under Controls |
| F1 / Escape | Open mod settings from the keyboard |
| Ctrl + F8 | Recenter head tracking / place the window in front of you |
| Ctrl + F11 | Cycle immersive VR, 3D window and room-view window |
| Ctrl + F7 | Request diagnostic source captures for both eyes |
| Ctrl + F9 | Toggle compositor color conversion for comparison/recovery |
| Ctrl + F10 | Toggle conservative culling expansion for comparison/recovery |

Video settings include **VR View**, **VR Render Resolution**, window
width/distance/height, camera offsets, world scale, HUD width/distance,
**VR Pause View** and **Recenter VR**. Choose **VR Pause View → Flat panel** to
restore the earlier pause presentation. See [settings and pause](docs/PCVR_SETTINGS.md).

The stereo window retains the desktop camera, field of view and framing, adding
binocular depth. Window width and height move/resize the complete picture.

For room view on Meta's PC runtime, enable **Developer runtime features** and
**Pass-through over Meta Quest/Horizon Link** under Settings → Developer (Beta
in older versions), accept Meta's notice,
then restart the game. If support or permission is unavailable, the stereo
window uses a black background. See [passthrough setup and test limits](docs/PCVR_PASSTHROUGH.md).

### VR development and testing

- [Build and OpenXR diagnostics](docs/PCVR_BUILD.md)
- [Settings, camera and pause](docs/PCVR_SETTINGS.md)
- [Render resolution and image quality](docs/PCVR_IMAGE_QUALITY.md)
- [Shared-work optimization and measured regions](docs/PCVR_PERFORMANCE.md)
- [Stereo window and passthrough](docs/PCVR_PASSTHROUGH.md)
- [Roadmap and remaining tests](docs/PCVR_ROADMAP.md)
- [Original v0.1.0-alpha checkpoint](docs/PCVR_V0_1_CHECKPOINT.md) — historical;
  later improvements are included on `main`.

The following sections retain the base project's desktop installation, renderer,
build and controller documentation. Standard desktop builds do not include this
fork's OpenXR integration unless explicitly configured for it.

## Base recompilation

The base project is an unofficial native recompilation of the Xbox 360 version
of Skate 3, supporting Windows, Linux and macOS. Its native renderer uses
Direct3D 12 or Vulkan. This fork's VR path uses Vulkan.

Upstream native-rendering showcase (desktop, not a VR demonstration):

<p align="center">
  <a href="https://youtu.be/ETXCOsip1Uo">
    <img src="https://img.youtube.com/vi/ETXCOsip1Uo/maxresdefault.jpg" alt="Skate 3 Recomp native rendering showcase" width="420">
  </a>
</p>

## How Do I Play?

For VR, use [Run in VR](#run-in-vr) above. These OS installation steps describe
the base desktop application and require an appropriate build/package.

Notes:

- The Windows version is the most tested, followed by Linux, and then macOS.
- On some hardware configurations, you may have a better experience running the Windows version through a translation layer like Proton rather than the native Linux build itself.
- The macOS ARM build is experimental and more prone to issues.

### Windows

1. Download the latest release Skate3Recomp-Windows.zip from the releases page.
2. Extract it anywhere you like, to a folder you control.
3. Run skate3.exe.
4. Click "Select ISO" to select your legally obtained copy of Skate 3.
5. Wait for the installer to extract the game files.
6. Click "Start Game".

### Linux

1. Download the latest release Skate3Recomp-Linux.zip from the releases page.
2. Extract it anywhere you like, to a folder you control.
3. Run skate3.
4. Click "Select ISO" to select your legally obtained copy of Skate 3.
5. Wait for the installer to extract the game files.
6. Click "Start Game".

### macOS (ARM / Experimental)

1. Download the latest release Skate3Recomp-macOS.zip from the releases page.
2. Extract it anywhere you like, to a folder you control.
3. Run the game by opening the skate3recomp app. Game files, saves and settings are kept in the folder containing the app, so keep it in a folder you control rather than in Downloads or Applications.
4. The first time, right-click the app and choose Open, or approve it under System Settings > Privacy & Security, before macOS will allow it to run.
5. Click "Select ISO" to select your legally obtained copy of Skate 3.
6. Wait for the installer to extract the game files.
7. Click "Start Game".

## Native Renderer

Since v2.0.0 the game no longer relies on emulating the Xbox 360 GPU. A native renderer draws the game directly through Direct3D 12 or Vulkan, covering the entire game: gameplay, menus, HUD, loading screens, videos, and the photo, replay, skater, and park editors. It runs exact ports of the game's own material shading for the world, characters, vehicles, and water, so the image stays at close visual parity with the original console output while running far faster and more efficiently.

- The native renderer is on by default. Settings > Video > Renderer switches between Native and Emulated live, and F5 hot-toggles between them at any time. If the native renderer ever hits an unrecoverable error, the game falls back to the emulated renderer and shows a corner indicator; F5 retries the native path.
- On Windows, a single build ships both graphics API backends: Settings > Video > Graphics API selects DirectX 12 or Vulkan (applied with Apply & Restart; DirectX 12 is preferred by default). Linux uses Vulkan, and macOS uses Vulkan through MoltenVK.
- Optional enhancements beyond the original game live under Settings > Video: MSAA up to 8x, enhanced real-time sun shadows with contact-hardening soft shadows, ambient occlusion, bloom, volumetric lighting, extended draw distance and world streaming, render scale up to 3x, and true ultrawide.

Known issues:

- Occasional texture and asset pop-in or brief flicker while streaming quickly around the map (also present in the emulated renderer)
- Issues with rendering skater customization options in the edit skater mode (skin, hair, clothing etc.)
- Hall of Meat currently not rendered properly (missing bone highlights etc.)
- Skate parks and park editing currently have some general visual parity / rendering issues

## Installing DLC

To use DLC, you must provide package files from your own legally obtained Xbox 360 DLC.

Create a `dlc` folder either beside the executable, inside the installed game folder,
or in the user data folder. Place the DLC package files in that
folder and start the game.

## Saves and Portable Mode

Saved games live in the user data folder by default (`%APPDATA%\skate3`, i.e.
`AppData\Roaming\skate3`, on Windows). Two portable options are available:

- Create a `saves` folder next to the executable and the game keeps saved games
  there instead. The folder must exist before launch, and existing saves are not
  migrated automatically - copy them over from the user data folder.
- Create an empty `portable.txt` file next to the executable to keep all user
  data in the executable's folder.

## True Ultrawide

The builds include an experimental true ultrawide mode: the native renderer draws the world at your display's full aspect ratio (21:9 and wider) with a matching wider field of view, while the HUD and menus stay centered and undistorted. Enable it via the Aspect Ratio setting or `skate3_ultrawide = true`. It requires the native renderer; with the emulated renderer the game presents in standard 16:9. Rendering more of the scene costs proportionally more GPU time.

## Controls

- Standard Xbox controls using an Xbox controller are the preferred and main input method.
- PlayStation (DualShock/DualSense), Switch and most generic controllers are supported through the SDL controller backend: set Settings > Controls > Controller Backend to SDL and restart the game. Steam Input through XInput also works. On Linux and macOS the SDL backend is always used, so these controllers work out of the box.
- Keyboard controls can be enabled in the game settings menu.
- Press Escape on keyboard or (RB + Start) on the controller to open the game settings menu. The chord can be changed in Settings > Controls.

### Keyboard Keybinds

- Left stick: W/A/S/D
- Right stick: mouse movement
- A/B/X/Y: Space/C/E/F
- LT/RT: RMB/LMB
- LB/RB: Q/R
- Left stick press: Shift
- Right stick press: MMB
- Back/Start: Tab/Return

## Building from Source

For the OpenXR game, follow [the VR build section](docs/PCVR_BUILD.md#build-the-openxr-game-on-windows).
The commands in this section build the ordinary desktop configuration.

All platforms build with CMake 3.25+, Ninja, and Clang 18 or newer - ReXGlue
requires Clang, so MSVC and Apple Clang are not supported. On Windows, install
[LLVM for Windows](https://releases.llvm.org/) and Ninja.

Clone with submodules:

```sh
git clone --recursive --branch main https://github.com/newtonsart/skate3-vr.git skate3recomp
cd skate3recomp
```

If you already cloned without submodules:

```sh
git submodule sync --recursive
git submodule update --init --recursive --jobs "$(nproc 2>/dev/null || echo 4)"
```

The build-time codegen needs an extracted game dump containing `default.xex` and
`data/webkit/EAWebkit.xex`. Put that dump in `game/`, or pass a path with
`SKATE3_GAME_DATA_ROOT`.

The codegen should also be given the Skate 3 Title Update 3 package - the same
`TU_12K2276_000000C000000.00000000000O3` file the in-game title update
installer downloads. Place it in the repository root under its original name,
or pass a path with `-DSKATE3_TITLE_UPDATE_PACKAGE=`. With the package
present, the build extracts the update patches and recompiles the TU3-patched
executables - the configuration used by release builds, whose runtime requires
the title update to be staged before the game will boot. Without the package,
codegen falls back to the unpatched retail image; that path is no longer
regularly tested, and `generate-all` reporting unresolved calls on a clean
retail dump is the usual symptom of building without the title update.

Generate the recompiled source first:

```sh
cmake --preset relwithdebinfo -DSKATE3_GAME_DATA_ROOT="$PWD/game"
cmake --build --preset relwithdebinfo --target generate-all --parallel
```

Then reconfigure so CMake sees the generated source lists and build:

```sh
cmake --preset relwithdebinfo -DSKATE3_GAME_DATA_ROOT="$PWD/game"
cmake --build --preset relwithdebinfo --parallel
```

For release packaging, use the `release` preset on Windows, the
`linux-release` preset on Linux, or the `macos-release` preset on macOS.

The native renderer's HLSL sources live in `src/native/shaders` and are
embedded into the build automatically. The matching Vulkan SPIR-V binaries are
pre-compiled offline with DXC and committed under `src/native/shaders/spirv`,
so building needs no shader tooling; the SPIR-V header only needs regenerating
after editing the HLSL.

## Ubuntu/Linux Build

These instructions target Ubuntu 24.04 LTS on x86_64. Other distributions need
the same toolchain shape: CMake, Ninja, Clang 20 or newer, Vulkan development
headers, GTK 3 development headers, and SDL-compatible audio/input development
packages.

ReXGlue requires Clang. Clang 20 is recommended on Ubuntu because it matches the
Linux toolchain used by the rexglue SDK CI and avoids Ubuntu 24.04's
Clang 18/libstdc++ `std::expected` feature-test mismatch.

Install LLVM's apt repository and dependencies:

```sh
sudo apt update
sudo apt install -y wget gnupg lsb-release software-properties-common
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 20
sudo apt install -y \
  git cmake ninja-build build-essential pkg-config p7zip-full \
  clang-20 lld-20 \
  libgtk-3-dev libx11-xcb-dev \
  libvulkan-dev vulkan-tools mesa-vulkan-drivers \
  libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev
```

Optional packages improve controller/input and diagnostics coverage in SDL:

```sh
sudo apt install -y libusb-1.0-0-dev libunwind-dev libibus-1.0-dev liburing-dev
```

Initialize submodules as described above, then configure, generate,
reconfigure, and build a development build:

```sh
cmake --preset linux-relwithdebinfo -DSKATE3_GAME_DATA_ROOT="$PWD/game"
cmake --build --preset linux-relwithdebinfo --target generate-all --parallel
cmake --preset linux-relwithdebinfo -DSKATE3_GAME_DATA_ROOT="$PWD/game"
cmake --build --preset linux-relwithdebinfo --parallel
```

Build a Linux release:

```sh
cmake --preset linux-release -DSKATE3_GAME_DATA_ROOT="$PWD/game"
cmake --build --preset linux-release --target generate-all --parallel
cmake --preset linux-release -DSKATE3_GAME_DATA_ROOT="$PWD/game"
cmake --build --preset linux-release --parallel
```

The release artifacts are:

```text
out/build/linux-release/skate3
out/build/linux-release/librexruntime.so
```

## macOS Build (Apple Silicon)

macOS builds use Homebrew LLVM - the `macos-*` presets expect the toolchain at
`/opt/homebrew/opt/llvm`. Vulkan is provided by MoltenVK; the build copies the
library next to the executable and writes a `MoltenVK_icd.json`, looking in
`$VULKAN_SDK/lib`, `/opt/homebrew/lib`, and `/usr/local/lib`.

```sh
brew install llvm cmake ninja molten-vk
```

Configure, generate, reconfigure, and build a release:

```sh
cmake --preset macos-release -DSKATE3_GAME_DATA_ROOT="$PWD/game"
cmake --build --preset macos-release --target generate-all --parallel
cmake --preset macos-release -DSKATE3_GAME_DATA_ROOT="$PWD/game"
cmake --build --preset macos-release --parallel
```

The release artifacts are `out/build/macos-release/skate3` and
`librexruntime.dylib`, plus the MoltenVK library and ICD manifest beside them.

## Running a Development Build

Run the built executable directly with a game dump. On Linux:

```sh
LD_LIBRARY_PATH="$PWD/third_party/rexglue-sdk/out/linux-amd64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  ./out/build/linux-relwithdebinfo/skate3 --game_data_root="$PWD/game"
```

On Windows:

```powershell
.\out\build\relwithdebinfo\skate3.exe --game_data_root="$PWD\game"
```

Keyboard-to-controller emulation is off by default; enable it in
Settings > Controls > Mouse & Keyboard Mode.

Fullscreen is on by default. Pass `--fullscreen=false` to start windowed.
On Windows, ultrawide displays are detected automatically in fullscreen. For an
ultrawide window, pass matching `--window_width` and `--window_height` values;
the native renderer then draws true widescreen frames at that aspect.

## rexglue Fork

`third_party/rexglue-sdk` is pinned as a Git submodule to the
`skate3-sdk-clean` branch of the Skate-specific rexglue fork. Clone
recursively or run:

```sh
git submodule sync --recursive
git submodule update --init --recursive --jobs "$(nproc 2>/dev/null || echo 4)"
```

The fork is based on rexglue's 0.8.0 release line and contains the Skate 3
runtime, codegen, input, timing, the Direct3D 12 and Vulkan backends used by
the native renderer, the settings overlay, and the Linux and macOS fixes
needed by this project.

## Credits

- [Skate3Recomp](https://github.com/mchughalex/skate3recomp) and its contributors,
  the native recompilation and renderer this VR fork builds on.
- [rexglue SDK](https://github.com/rexglue/rexglue-sdk), the recompilation SDK
  used by this project.
- [Xenia](https://github.com/xenia-project/xenia), whose Xbox 360 research and
  tooling have helped the broader recompilation ecosystem.
