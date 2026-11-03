# VR settings and pause over the stereo world

The OpenXR build now draws the recompilation's existing settings menu into a
Vulkan texture and composes it as an additional headset panel. Gameplay keeps
its independent eye views behind the settings panel. This is the mod's settings
menu, accessible from inside the game; it does not add an entry to the retail
game's APT pause-menu assets.

## Access and options

- **RB + Start** opens settings with the default controller chord. If you
  changed Menu Shortcut under Controls, use that combination instead.
- **F1** or **Escape** opens settings from the keyboard.
- The existing controller navigation selects Video and its rows, steps values
  left/right, and goes back with B. Reset uses Y (R on keyboard).
- Changes apply immediately and use the existing per-user settings file.

Video includes VR mode and render resolution, window width/distance/height, additional immersive-camera
distance/height, world scale, HUD width/distance, pause presentation and recenter.
Camera distance is an offset from the game's current follow camera, not an
absolute distance to the skater. Positive values move backward, positive height
moves upward. Both default to zero; head orientation remains independent.
Offsets and positional tracking use the configured world scale. Smoothing and
collision avoidance for the additional offsets have not been added.

Window controls apply in window modes; camera/HUD controls apply in immersive
mode. Recenter also has the existing Ctrl+F8 shortcut. World scale affects both
modes. Ordinary desktop runs do not render the additional settings surface.

**VR Render Resolution** offers Desktop (compatibility), HMD 75%, HMD 100% and
HMD 125%. The HMD choices size the source render target from OpenXR's recommended
eye dimensions, independently of the desktop output resolution. The HUD and
window retain the display's logical aspect. HMD 100% is the first quality setting
to compare; Desktop restores the original path. See [PCVR_IMAGE_QUALITY.md](PCVR_IMAGE_QUALITY.md)
for measured dimensions and validation limits.

## Live window and camera editors

The action immediately after **VR View** opens a compact control panel instead
of the full settings screen. The normal settings dialog remains open underneath
to keep gameplay input blocked, but only the editor is drawn. The world remains
stereoscopic. Settings blur is disabled in VR, and the live editor also removes
the captured pause blur from its per-eye scene copy.

In window modes, select **Move window**:

| Control | Action |
| --- | --- |
| Left stick left/right | Move the window horizontally |
| Left stick up/down | Raise/lower the window |
| Right stick up/down | Bring the window closer / move it farther away |
| LB / RB | Reduce / increase width, preserving aspect ratio |
| A | Place the window and save size/distance/height preferences |
| B | Cancel and restore the previous placement and size |

The window stays upright and anchored in LOCAL space while you adjust it.
Horizontal displacement is session-only; no persistent room anchor is created.
Ctrl+F8 clears that horizontal offset and recenters the window in front of you.
Movement is limited to ±8 m horizontally, ±2 m vertically, 0.5–8 m distance
and 0.5–6 m width. Stick deadzones and a clamped time step prevent drift/jumps
when opening the editor or after a slow frame. Release the opening button and
sticks before adjusting. Changing VR mode or closing settings cancels an
unconfirmed edit.

In immersive mode, select **Adjust camera** during gameplay. Left stick up/down
changes camera height; right stick up/down changes the additional camera
distance. The existing ranges remain −2–4 m height and −2–8 m distance. A saves,
B restores the previous values. The compact panel sits below the view so you
can inspect the skater and surroundings while making changes. A live world
preview requires a loaded, fresh gameplay scene; it cannot show an unloaded map.

Keyboard alternatives: WASD moves horizontally/vertically, up/down arrows adjust
distance, minus/plus resize the window, Enter applies, Escape cancels. A gamepad
can disconnect without confirming an edit; keyboard controls remain available.

The completed UI image carries its editor mode alongside the Vulkan image.
OpenXR crops that image to the editor's lower strip and places the strip below
the scene; ordinary settings retain their full panel. This avoids a full black
rectangle covering the preview and preserves the existing color/queue path.

## Pause presentation

Start still opens the original game pause menu. **VR Pause View: Menu over 3D
world** is the new default: when the game keeps publishing a fresh world, both
eyes continue to render it with head tracking, and the captured pause UI uses
the comfortable menu transform in front of it. Window modes retain their stereo
window. The mod's own settings use a separate opaque quad over the scene; their
background does not replace either world eye.

Boot menus remain flat until gameplay has been seen. Empty/stale scenes and
loading screens retain the previous flat presentation. If a transition changes
from world to loading between eye renders, the flat image goes to both eyes.

For compatibility, select **VR Pause View: Flat panel**, or append
`--skate3_vr_pause_in_world=false` to a normal launch. This restores the prior
pause route. The existing `skate3_native_render_scene_pause_native` must also
remain enabled for the native stereo pause backdrop.

## Implementation and checks

The same ImGui draw data is replayed into the offscreen target: navigation runs
once and no desktop/window capture is used. Uploads, desktop UI commands and
the additional pass share one presenter submission/fence, so font and vertex
buffers stay valid. The target matches the presenter's format and render-pass
dependencies to reuse its pipelines. A separate XR swapchain holds the settings
panel. The render thread tries to acquire the completed UI image without waiting
for UI recording; while busy, it retains the last submitted panel. Game and
window projections, color conversion, Vulkan queue locks and shadow sharing
remain in use. The additional UI draw/copy happens only while settings are open
in VR (or during the explicit diagnostic below).

Local validation on 2026-11-02:

- Windows RelWithDebInfo game and math tests compiled.
- Stereo tests include camera offsets, zero-offset equivalence and invalid
  offsets; pause eligibility covers boot, gameplay, pause, loading/stale and
  invalid-age cases. Movie-transition and SDK patch tests also pass.
- A separate desktop test with the actual Khronos validation layer enabled
  rendered the settings surface. Its GPU readback was visually inspected and
  showed the VR/camera/HUD/pause/recenter rows. No VUID messages were recorded.
- The run also logged XMA audio-buffer and shutdown heap-release errors. These
  same error classes occur in earlier baseline/session logs; this menu check
  does not diagnose or resolve them.

To exercise the settings surface without a headset, launch with
`--skate3_vr=false --skate3_vr_settings_test=true` and a separate user-data root.
This opens settings and writes ignored `out/vr-settings.ppm` once after 30
completed UI passes. It proves the source UI render/readback, not OpenXR
composition, controller operation or stereo pause behavior in a headset.

Local live-editor checks on 2026-11-03:

- Windows RelWithDebInfo game, OpenXR probe and stereo/editor-math targets built.
  Tests cover opening-button suppression, stick deadzones, all placement axes,
  camera limits, apply/cancel, horizontal anchor rotation and UI crop bounds.
  Movie-skip and SDK patch/idempotency regressions passed.
- Separate desktop diagnostic runs with `skate3_vr_settings_test=true` and
  `skate3_vr_edit_mode=1` / `2` rendered both compact panels. For mode 1, also set
  `skate3_vr_mode=1`; mode 2 requires immersive mode 0. Actual Vulkan UI readbacks
  at 1280×720 were visually inspected; text and controls fit the intended crop.
  This is source-UI verification, not validation of OpenXR cropping or live
  gamepad operation in the headset.
- The Khronos validation layer was active. No VUIDs appeared while rendering
  these panels. Shutdown logged `VUID-vkDestroySemaphore-semaphore-01137` in both
  editor runs and in a comparison run using the full settings menu with both
  live editors disabled. The SDK presenter's semaphore teardown remains a
  separate unresolved issue; this change does not claim to fix it.
- Quest Link was unavailable (`XR_ERROR_FORM_FACTOR_UNAVAILABLE`, −35).
  Spatial placement, stereo depth, panel comfort, live camera preview in
  gameplay and controller reconnection still require headset testing.

Still required: headset checks of both menus, legibility and depth, moving the
head while paused, adjusting/saving/reopening settings with a controller, and
pause/load/unpause transitions. The user could not provide a headset test during
this implementation; no visual headset success is claimed.
