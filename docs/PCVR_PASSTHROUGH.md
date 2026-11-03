# Stereo window and room view

Experimental, opt-in implementation. Immersive VR remains the default. The user
tested the window and reported a cropped/zoomed view. The desktop-framing fix
below is compiled and math-tested, but still needs headset confirmation.
The **camera background has not yet been visually validated**: the Meta runtime
did not expose passthrough during the recorded user test.

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
These place and resize the complete desktop view; they do not zoom or crop it.
For direct placement, select **Video → Move window**: left stick moves it
horizontally/vertically, right stick changes distance, LB/RB resize it, A places
it and B cancels. A compact panel leaves the scene visible while adjusting.
The window remains anchored during the session; no saved room anchor is needed.
Touch controllers can also point and grip-drag the panel in **Move window**,
with a blue/green target marker. The held-hand stick adjusts reach and the
other stick resizes it; A confirms, B cancels. See [controller placement](PCVR_CONTROLLERS.md#point-and-grab-the-window).
The existing **Field of View** setting controls the in-game framing in window
mode. Immersive camera offsets and the headset's eye FOV do not apply there.

## Meta Quest Link setup

For Meta's PC runtime, open Settings → **Developer** (or **Beta** in older
versions) and enable **Developer runtime features** and **Pass-through over
Meta Quest/Horizon Link**, accepting Meta's notice. Enter Quest Link and restart
the game after changing runtime extension settings.
This is Meta's development passthrough path, with availability governed by the
installed runtime. See [Meta's setup instructions](https://developers.meta.com/vr/documentation/unreal/unreal-passthrough-use-over-link/)
and the [current Link developer-settings guide](https://docs.unity3d.com/Packages/com.unity.xr.meta-openxr@2.5/manual/get-started/link.html#enable-developer-features).

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

The user session on 2026-11-02 reached mode 2 and logged **room view unavailable**;
it deliberately retained the stereo window against black. A fresh probe on
2026-11-03 still reported `Passthrough extension enabled: no`, with no available
Link headset. When that line says `no`, the game cannot create a camera layer.
The `--passthrough` probe now explains the missing runtime setup before trying
headset detection, so an absent headset cannot hide that diagnosis. When support
is reported, use the camera-only probe before testing the game composition.

Other OpenXR headsets retain immersive VR and the stereo-window path. Room view
requires the optional `XR_FB_passthrough` extension and reported system support;
OpenXR alone does not guarantee camera composition on every runtime.

## Soft window edges

In **Video → VR Window Edges**, choose **Sharp**, **Soft** or **Wide**.
Sharp is the default; Soft and Wide use 4% and 8% of panel height for the fade.
Both eyes use the same mask in panel coordinates, preserving stereoscopic
depth and the existing desktop framing. The fade is transparency over the
runtime's room-view layer, not an image blur or a black vignette. The centre
remains fully opaque; HUD/text near the outside border fades with the game.
The independent mod-settings panel stays opaque and legible.

This setting only takes effect with mode 2 and an active passthrough layer.
Room-view failure retains the full opaque rectangle. Changing back to Sharp,
the black-background window or immersive mode removes the feathering.
It does not place the skater on your physical floor or model furniture,
real-world occlusion, room lighting or shadows.

The existing color compute pass writes premultiplied RGB and alpha before the
window is submitted with OpenXR's texture-alpha blend flag. The mask uses
panel aspect, not eye-texture aspect, to keep the physical fade width uniform.
The pass also works when Ctrl+F9 disables color decoding; all transfer barriers
follow the compute path in that case. Default color conversion retains its
existing dispatch; only the edge arithmetic is added. The compositor performs
the blend; camera pixels are not retrieved or recorded by this application.
See the [OpenXR alpha contract](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrCompositionLayerFlagBits.html).

Local checks on 2026-11-03: the game, shader and patched settings menu compiled.
`skate3-vr-color-test` dispatched the actual shader on the RTX 4080 and checked
all RGBA pixels across 18 readbacks: decoding on/off, Sharp/Soft/Wide, and square,
wide and portrait panel aspects. Vulkan validation produced no messages in
that test. Stereo/window activation and invalid-parameter checks passed, as
did SDK patch/idempotency tests. To repeat the GPU check without a headset:

```powershell
cmake --build out/build/relwithdebinfo --target skate3-vr-color-test
out/build/relwithdebinfo/skate3-vr-color-test.exe
```

The Meta probe still reported `Passthrough extension enabled: no`. The border's
appearance over actual camera images, HUD readability and performance in the
headset remain unverified.

## Rendering and verification

Gameplay renders the same scene independently for both eyes, preserving the
desktop game camera's pitch, yaw, field of view and clipping planes. Each eye
uses a parallel camera with an off-axis stereo shift from its tracked position.
The centered head reproduces the desktop view matrix/projection exactly after
coordinate conversion; the physical panel bounds no longer replace the game
FOV. Head rotation changes the view of the panel; translation adds parallax
within the scene. The convergence plane is `window distance × world scale`
game units from the original camera. HUD and frontend/pause menus fit the
panel; mono menus use one quad visible to both eyes.

Window gameplay uses two LOCAL-space quad layers with LEFT/RIGHT visibility.
Room view adds a reconstruction passthrough layer beneath them, following the
[Khronos layer contract](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerPassthroughFB.html).
The runtime supplies the camera view; the application does not retrieve or save
raw camera images. Leaving room mode destroys the passthrough layer and feature
before their parent session. Existing Vulkan queue synchronization, color
conversion and shared scene/shadow work are retained.

`skate3-stereo-math-test` covers desktop-frustum preservation at 45/60/90/110°
FOV, asymmetric projections, multiple window distances and world scales,
screen-plane convergence, near/far disparity,
head translation/rotation, window recentering, pitched game cameras, invalid
parameters and repeated unsupported-passthrough enable/disable. The disposable
SDK patch test checks repeat application and includes the new settings rows.

Local checks on 2026-11-02: Windows RelWithDebInfo game, probe and math targets
compiled; the math/window/unsupported-passthrough checks and movie-skip test
passed; all three SDK patch tests passed. A separate desktop smoke profile
initialized Vulkan/game runtime without error/critical records in the inspected
run and persisted the four new settings. That desktop run had VR disabled and
does not validate the headset composition or room-view performance.

Local checks on 2026-11-03: the updated Windows game, probe and math targets
compiled; desktop-framing/stereo checks and the movie-skip regression passed.
The unsupported-runtime probe produced the expected setup error before headset
detection. The user could not enable Link/passthrough for a visual retest.

Still required with the headset available: confirm the room background, stereo
fusion and depth direction; test leaning, mode switching and recentering;
check HUD, pause/loading screens, color, shadows and performance in motion;
check permission failures and removal/reconnection during room mode.
