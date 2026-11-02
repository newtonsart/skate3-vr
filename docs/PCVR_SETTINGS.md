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

Still required: headset checks of both menus, legibility and depth, moving the
head while paused, adjusting/saving/reopening settings with a controller, and
pause/load/unpause transitions. The user could not provide a headset test during
this implementation; no visual headset success is claimed.
