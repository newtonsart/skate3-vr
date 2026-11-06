# VR settings and pause over the stereo world

VR settings are available directly in the game's **Options → View** page,
using the original menu's controls and descriptions. Long categories are split
into numbered pages with **Previous page** and **Next page** action rows; each
page contains at most eight rows including navigation. Back returns to the
Options root. Settings pending a restart survive page changes. The mode,
resolution, window, camera, HUD and comfort settings apply immediately and
share the existing settings file. Rows unavailable in the current VR mode
are disabled. The VR rows appear only in VR runs or the explicit desktop
settings diagnostic.

**Move window** and **Adjust camera** open the existing compact live editor.
Apply or cancel returns to the native menu after the confirmation buttons
are released. The game's HUD/menu draws are hidden in the world-eye preview
while editing, so the native Options page does not obscure the scene.
Recenter and frame capture are also native action rows.

The alternative **RB + Start → Video** panel remains available. The OpenXR
build draws that panel into a Vulkan texture and composes it in the headset,
with independent eye views behind it. The same surface serves live editors
and system dialogs.

**FPS Counter** also appears in a small head-relative panel at the upper right
in all VR modes, using the same option and guest FPS measurement as the desktop
counter. It follows the widget's actual bounds, with a translucent background
and transparent surrounding pixels. Turning the option off hides it immediately. The panel refreshes four
times per second and reuses the existing UI mirror and swapchain; full settings
and system dialogs retain their usual presentation. It reports game output
cadence, not the configured headset refresh or compositor-generated frames.

The native VR row changes compile and pass `python cmake/test_vr_options_menu.py`
(run in a C++ developer shell). Headset navigation and editor transitions
still require hardware validation.

## Access and options

- **Start → Options → View** opens the native VR controls.
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

Game system dialogs, including text-entry prompts for names, use the same
headset panel. Type with the PC keyboard and press Enter to accept, or use
the dialog's existing OK/Cancel buttons. No second input widget or new virtual
keyboard is created; desktop and headset show the same dialog and text.

**VR Window Edges** offers Sharp (default), Soft and Wide in room-view mode.
Soft/Wide fade the outer 4%/8% of panel height into active passthrough, with
equal physical fade widths on all sides. The centre stays opaque; only the
border becomes transparent. Corner fades combine smoothly. HUD content near
the edge can also fade; choose Sharp to retain the full border. The setting
applies immediately and is saved per profile. Without active passthrough,
the window remains opaque. It does not add room geometry, occlusion or lighting.

**VR Window Depth** offers Natural (default), Reduced and Flat. Natural calibrates
binocular disparity to the physical window width, its aspect and the game FOV,
rather than using window distance as the game-space convergence distance.
Reduced halves stereo disparity; Flat renders the original camera in both eyes.
The head centre remains the stereo reference so walking does not steer the game
camera. The full desktop framing is retained.

**Face Viewer While Placing** defaults on. Inside Move window it turns the panel
towards the tracked head position, including height, without moving its centre or
adding roll. Orientation is held during a grip drag. **A** anchors both position
and orientation; head movement afterwards does not rotate it. **B** restores the
previous placement and orientation. Off uses the recentered orientation. The
pointer uses the same actual panel rotation for hit testing.

**VR Editor Comfort View** defaults on. Photo capture/editor/card flows use one
original-camera render, preserving the game's screenshot and postfx framing.
In immersive mode they appear in a readable front window. Selected park-editor /
object-mover meshes trigger a stereo front window with the original game camera
orientation, so placement markers and UI retain their desktop relationship.
It returns to the chosen mode when that flow ends. Ordinary pause is unchanged.
Off provides the previous presentation for comparison. Detection of an object
editor relies on its selected-mesh signal; menus with no selected object may
retain the normal HUD view.

**VR Loading Symbol Only** defaults on in immersive gameplay. It removes the
captured loading group’s full-screen solid black fill, including fade-in/out,
while replaying the spinner tiles unchanged. Recognition checks the draw layout,
screen coverage and the small lower-right spinner group; unrelated solid fades
and ordinary menus are retained. Off restores the original loading backdrop.

Full-screen solid dim/fade fills are also kept at full eye coverage, rather than
shrunk into the readable HUD panel. This avoids a floating semi-opaque rectangle
in saving-video and other modal overlays. Text and controls keep their readable
panel transform; the game’s backdrop colour/opacity are preserved.

**VR Render Resolution** offers Desktop (compatibility), HMD 75%, HMD 100% and
HMD 125%. The HMD choices size the source render target from OpenXR's recommended
eye dimensions, independently of the desktop output resolution. The HUD and
window retain the display's logical aspect. HMD 100% is the first quality setting
to compare; Desktop restores the original path. See [PCVR_IMAGE_QUALITY.md](PCVR_IMAGE_QUALITY.md)
for measured dimensions and validation limits.
Flat frontend/loading screens and original-camera photo flows temporarily use
the desktop render extent. This avoids compressing their original UI into a
portrait eye target. The chosen HMD resolution is restored for stereo gameplay;
the saved quality preference is not changed.

**Capture VR frame** under Video (or Ctrl+F7 with the game focused) captures
the next available game frame, including flat menus and loading screens.
It saves local `out/vr-menu-*.ppm` or `out/vr-eye-*.ppm` files next to the running
application’s working directory. This contains the game image, not camera passthrough.

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

With Touch, release the opening inputs, point at the window (blue ring), then
hold that hand's grip to move it (green ring). Release to anchor it. While
holding, its stick up/down changes distance and the other hand's stick up/down
changes size. A confirms, B restores the whole edit. Touch grips are reserved
for grabbing inside this editor; regular-gamepad LB/RB still resize normally.
Tracking/focus loss releases the grab and requires a fresh grip after recovery.
See [controller placement and validation](PCVR_CONTROLLERS.md#point-and-grab-the-window).

The window centre stays anchored in LOCAL space while you adjust it; automatic
facing can rotate it towards the head only in the placement editor, and grip
dragging holds its current rotation. Confirmation fixes its orientation too.
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
remain in use. The additional UI draw/copy happens only while settings or a game system dialog are open
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

Follow-up on 2026-11-03 after a roughly 15-minute user test: the user reported
good basic operation, but uncomfortable window depth, an opaque loading patch,
and awkward photo/object UI. The new window math checks horizontal and vertical
far disparity against IPD across screen widths, FOVs, aspects and world scales;
it also checks unchanged centre framing, Flat mode and facing towards the head.
Pointer tests include an off-centre grab on a tilted, viewer-facing panel.
Editor-policy and loader-group tests cover defaults, photo priority, geometry
bounds, all fade phases, unrelated fills, small sprites and ordinary menu content.
SDK patch/idempotency checks pass. The game’s HUD pixel shader remains unchanged;
loading cleanup and full-screen backdrop placement happen in the draw replay.

The Meta runtime now reports a Quest 3 and passthrough support. An installed
trial was opened with the user's existing game/save folders and submitted
live stereo gameplay frames without logging a VR-render shutdown. The prior
binaries and settings were backed up locally before replacement. The user
confirmed improved window depth and viewer-facing rotation, then confirmed the
flat-menu aspect correction. Actual readbacks show the original squeezed
2272×2464 loading source and a correct full-frame 2560×1440 frontend after the fix;
stereo gameplay restores the user’s chosen HMD extent. The loader draw trace
identified its solid fill and spinner sequence; the runtime logs its removal.
The first filter missed the loading fill’s fade phases; all phases are now
handled, and generic modal backdrops use full eye coverage. The user confirmed
that the loading animation and saving-video overlays both look better in the
final installed trial. Broader photo-mission/object-editor flows still need
testing; this does not claim every game menu has been validated.

Further fixes in 0.2.5-alpha:

- Viewer-facing rotation now runs only in Move window. Confirmation anchors
  the rotation; cancellation restores it with the previous position/size.
  Math checks cover idle head movement, grip, apply, cancel, disabled facing
  and recentering.
- Game system dialogs are mirrored through the existing settings surface,
  including text-entry prompts. The editor crop is disabled for these dialogs;
  a stale settings image is not reused as a system dialog. Input and guest
  completion still use the original dialog. Escape does not open settings
  over an active system prompt.
- The reported photo-selection slowdown was a CPU readback cost. Before the
  fix, native photo tiling/writing averaged 58,412 us per frame in that flow.
  Copying the mapped source to cached memory before the existing conversion
  reduced this to 1,248 us across 1,935 writes in the installed trial; stereo
  submissions returned to approximately 90/s. Target dimensions, pixel format,
  guarded guest write and shutter-burst handling are unchanged.

The user confirmed that photo selection recovered its fluency and sound, but
reported a remaining hitch when placing the selected photo on the magazine.
The first guest-image write still cost 58–70 ms because it triggered individual
page-watch invalidations. The final trial uses the runtime's existing exact-range
write notification before the guarded copy. Its first write measured 616 us;
the full photo flow averaged 977 us across 1,926 writes. An isolated check with
the real runtime verified a single full-image notification, identical bytes,
untouched adjacent pages and preservation of the remaining page watches. The
user then confirmed that the magazine transition's hitch/audio fault was gone.

The user also reported needing a desktop click before typing. Opening a system
dialog in VR now requests desktop foreground/keyboard focus once. The user
confirmed typing directly without clicking the PC window. Windows controls
[foreground activation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow);
the application does not repeatedly reclaim focus if the user switches away.
Saved-photo content and broader photo-mission coverage still need more tests.

The user later reported a persistent offscreen “Sign up” prompt in immersive VR.
The reduced HUD now clips orthographic UI to the original game-frame rectangle
after applying its scale, per-eye offset and widescreen band. Full-view modal
backdrops and perspective world markers keep their existing coverage; the
original scissor is restored before later passes. Pixel-centre checks cover
fractional per-eye placement, mirrored scales, band width, offscreen/empty
rectangles and invalid inputs. Actual eye captures include a UI quad at
original NDC Y −1.514…−1.336, outside the desktop frame. The corrected eye image
and a full-frame loading-menu capture were inspected. The user confirmed that
“Sign up” disappeared while the regular HUD remained correct.
