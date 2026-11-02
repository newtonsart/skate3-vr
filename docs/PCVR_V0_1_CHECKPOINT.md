# PCVR v0.1.0-alpha — first playable checkpoint

2026-11-02, branch `vr-development`. Experimental source checkpoint, not a
finished release. The base game's version number remains unchanged.

## Confirmed on the target headset

Meta Quest 3 / Quest Link / RTX 4080, Vulkan, original gamepad:

- Real per-eye world rendering through a shared ReXGlue/OpenXR Vulkan device.
  The user confirmed that the world fuses correctly in the headset.
- Frontend menus are visible and the difficulty selector advances after the
  frontend movie-skip timing fix.
- HUD readability is improved; the pause menu is complete, centered and readable.
- Ground and NPC appearance recovered after removing the regressive culling
  replacement. A conservative expansion of the original main-camera planes
  improves rear-view visibility without those regressions in the tested area.
- App-owned color conversion fixes the black-image regression and improves
  brightness according to the headset test.
- Shared queue locking covers Vulkan submissions and all four OpenXR calls
  permitted to touch that queue. The non-validation run passed the previous
  submission failure point and continued into gameplay.
- The user no longer observed near-ground spikes while accelerating, including
  after Quest Link ASW was restored to its original automatic mode, and reported
  improved performance. This is qualitative feedback, not a controlled benchmark.

## Automated and runtime checks

- Windows RelWithDebInfo game and OpenXR diagnostic targets compiled.
- `skate3-movie-skip-test`: early skips wait for the frontend transition;
  boot skipping, pending requests and movie boundaries are covered.
- `skate3-stereo-math-test`: eye separation/parallax, asymmetric projection,
  head yaw, HUD placement, conservative culling expansion, both plane distance
  conventions/inside signs, unrelated cameras, orthographic volumes and invalid
  inputs are covered.
- `cmake/test_openxr_interop.py`: 3 tests passed, including repeat application
  and rejection of source checkouts/unrecognized patch anchors.
- `cmake/test_extract_game_iso.py`: 4 tests passed.
- Vulkan validation identified and verified corrections for feature-chain
  conflicts and missing shader capabilities. A bounds guard rejected stale mesh
  draws that previously exceeded their resident index buffer.
- The final queue-synchronization run used neither Vulkan validation nor image
  captures by default. Sample render/copy CPU intervals often measured around
  7–10.4 ms; these include GPU waits and exclude parts of the game frame.

## Run and recovery controls

See [PCVR_BUILD.md](PCVR_BUILD.md) for source-build dependencies and the disposable
SDK patch. With a configured build and local game data, from the repository root:

```powershell
./out/build/relwithdebinfo/skate3.exe --gpu_backend=vulkan --skate3_vr=true --game_data_root="$PWD/game" --user_data_root="$PWD/out/runtime-test" --skate3_native_render_scene_msaa=1 --vulkan_validation_enabled=false --vulkan_log_debug_messages=false --skate3_vr_capture=false
```

The tested path uses MSAA 1. Color conversion and conservative culling expansion
default on in VR. Keep Quest Link active before launching.

- Ctrl+F7: request diagnostic source images for both eyes.
- Ctrl+F8: recenter the view.
- Ctrl+F9: toggle compositor color conversion for comparison/recovery.
- Ctrl+F10: toggle the conservative culling expansion for comparison/recovery.

## Still required

- Longer sessions, more maps, high-speed traversal and streaming transitions.
- Save/load, death/restart, photo mode and additional menu flows.
- Headset removal/reconnection, runtime/session loss and device recovery.
- Visibility and shadow checks for full turns and large positional movement;
  the culling expansion is finite and cannot reconstruct streamed-out geometry.
- Separate GPU timestamp measurements, latency and sustained 72/90 Hz checks.
- Camera comfort limits, distance/height/smoothing settings and the requested
  stereoscopic-window mode, which is not implemented in this checkpoint.

Game assets, generated retail code, updates, user data and local test logs remain
excluded from Git. Pre-existing changes inside the original SDK submodules are
preserved.
