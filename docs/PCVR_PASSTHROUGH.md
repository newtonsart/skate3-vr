# Stereo window and room view

Experimental, opt-in implementation. Immersive VR remains the default. The new
window and camera background have **not yet been visually validated in a
headset**; the user could not enable the Meta settings during development.

## Launch and controls

Launch the existing OpenXR build with Quest Link active, adding
`--skate3_vr_mode=2` for the window with a room background:

```powershell
./out/build/relwithdebinfo/skate3.exe --gpu_backend=vulkan --skate3_vr=true --skate3_vr_mode=2 --game_data_root="$PWD/game" --user_data_root="$PWD/out/runtime-test" --skate3_native_render_scene_msaa=1 --vulkan_validation_enabled=false --vulkan_log_debug_messages=false --skate3_vr_capture=false
```

- Ctrl+F11 cycles immersive VR → stereo window → stereo window with room view.
- Ctrl+F8 places the window in front of the current head position and yaw.
- F1 on the PC opens the host settings. Under Video, **VR View** selects the
  mode; **VR Window Width / Distance / Height** adjust the panel in meters.
  These settings apply immediately and are saved in the existing settings file.
  In VR these settings also appear as a panel in the headset, over the stereo
  scene. The default controller shortcut is RB + Start. Camera, HUD and pause
  options are described in [PCVR_SETTINGS.md](PCVR_SETTINGS.md).
  Hotkey mode changes are temporary until saved there.

Mode `0` is immersive, `1` is the window against black, and `2` requests room
view. Without passthrough support or permission, mode 2 retains the stereo
window against black and reports the reason in the log and VR View description.
It does not disable VR or fall back to identical gameplay images for both eyes.

The default window is 2.4 m wide, 2 m away, and level with the recenter position.
Optional command-line settings are `--skate3_vr_window_width=2.4`,
`--skate3_vr_window_distance=2.0`, and `--skate3_vr_window_height=0.0`.

## Meta Quest Link setup

For Meta's PC runtime, open Settings → Beta and enable **Developer Runtime
Features** and **Passthrough over Meta Quest Link**, accepting Meta's notice.
Enter Quest Link and restart the game after changing runtime extension settings.
This is Meta's development passthrough path, with availability governed by the
installed runtime. See [Meta's setup instructions](https://developers.meta.com/vr/documentation/unreal/unreal-passthrough-use-over-link/).

The new diagnostic prints extension and system support:

```powershell
./out/build/relwithdebinfo/src/vr/skate3-openxr-probe.exe --passthrough
```

With support and a renderable headset, it submits a camera-only layer for about
ten seconds. It does not render the game. Successful API submission still needs
confirmation in the headset. On this PC during implementation, the Oculus
runtime did not expose the passthrough extension and headset detection returned
`XR_ERROR_FORM_FACTOR_UNAVAILABLE` (-35). That is an unavailable test environment,
not a successful camera test.

Other OpenXR headsets retain immersive VR and the stereo-window path. Room view
requires the optional `XR_FB_passthrough` extension and reported system support;
OpenXR alone does not guarantee camera composition on every runtime.

## Rendering and verification

Gameplay renders the same scene independently for both eyes, preserving the
game camera's pitch and yaw. Each eye uses an off-axis projection calculated
from its tracked position and the fixed panel bounds. Head rotation changes
the view of the physical panel; translation adds parallax within the scene.
The screen plane has zero binocular disparity. HUD and frontend/pause menus
fit the panel; mono menus use one quad visible to both eyes.

Window gameplay uses two LOCAL-space quad layers with LEFT/RIGHT visibility.
Room view adds a reconstruction passthrough layer beneath them, following the
[Khronos layer contract](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerPassthroughFB.html).
The runtime supplies the camera view; the application does not retrieve or save
raw camera images. Leaving room mode destroys the passthrough layer and feature
before their parent session. Existing Vulkan queue synchronization, color
conversion and shared scene/shadow work are retained.

`skate3-stereo-math-test` covers screen-plane convergence, near/far disparity,
head translation/rotation, window recentering, pitched game cameras, invalid
parameters and repeated unsupported-passthrough enable/disable. The disposable
SDK patch test checks repeat application and includes the new settings rows.

Local checks on 2026-11-02: Windows RelWithDebInfo game, probe and math targets
compiled; the math/window/unsupported-passthrough checks and movie-skip test
passed; all three SDK patch tests passed. A separate desktop smoke profile
initialized Vulkan/game runtime without error/critical records in the inspected
run and persisted the four new settings. That desktop run had VR disabled and
does not validate the headset composition or room-view performance.

Still required with the headset available: confirm the room background, stereo
fusion and depth direction; test leaning, mode switching and recentering;
check HUD, pause/loading screens, color, shadows and performance in motion;
check permission failures and removal/reconnection during room mode.
