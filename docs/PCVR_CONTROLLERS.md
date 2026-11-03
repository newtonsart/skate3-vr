# Quest Touch as an Xbox controller

Wake both Touch controllers in an active Quest Link PCVR session, then launch
any VR mode. No virtual-controller driver or external remapping tool is needed.
The original game still sees player 1 using its standard Xbox controls.

| Touch input | Xbox input |
| --- | --- |
| Left/right thumbsticks | Left/right sticks, including the right-stick flick for tricks |
| A / B (right), X / Y (left) | A / B / X / Y |
| Index triggers | Analogue LT / RT |
| Grip triggers, pressed past 55% | LB / RB |
| Thumbstick click, centred | L3 / R3 |
| Left Menu | Start / pause |
| Left Menu + X | Back; X and Start are suppressed while the combination is held |
| Hold left stick click and tilt | D-pad, including diagonals; left stick axes and L3 are suppressed while tilted |
| Right grip + left Menu | Default RB + Start mod-settings shortcut |

For menu navigation, use the left stick and A/B as with a normal gamepad.
The same inputs operate the live window/camera editors. Their existing input
capture prevents the skater from responding while adjusting settings.
The right Meta/system button is not bound by the application.

Skating still uses sticks/buttons, without motion-based tricks or Touch
vibration. Window placement also supports tracked controller pointing and grip
as described below. No controller/hand models or free-floating laser beam are
drawn; a target marker appears on the window. Keep a regular gamepad available
during this experimental stage.
Only the standard OpenXR Oculus Touch interaction profile has explicit bindings;
other controller families are not claimed as supported.

## Input handling

Actions are created and attached before the OpenXR session begins. Sampling
runs once per rendered frame on the OpenXR render thread; guest/UI input reads
a small mutex-protected snapshot, without issuing OpenXR calls from those threads.
No extra stick deadzone or smoothing is applied; Xbox scaling preserves the
game's own stick handling. Triggers remain analogue; grips become shoulder buttons.

The input system merges Touch with existing controller drivers: button OR,
maximum triggers and the axis with the largest magnitude. Both feed player 1;
Touch is not a second player. Leave the unused controller's sticks neutral.
Gameplay input is suppressed by the existing settings/dialog capture, while
raw controller input remains available for UI navigation and the menu chord.

Loss of session focus, inactive actions, a stopped session or a render failure
clears Touch input. Snapshots older than 250 ms are treated as disconnected,
so a stalled renderer cannot hold a trigger or button indefinitely. A controller
input error is logged and disables Touch for that run without stopping stereo
rendering or removing the regular gamepad fallback.

Bindings follow the [Khronos Oculus Touch interaction profile](https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html#_oculus_touch_controller_profile).

## Point and grab the window

1. Choose **3D window** or **3D window + passthrough**, then open
   **Video → Move window**. Release the opening button, sticks and grips.
2. Point either controller at the window. A **blue ring** marks the target.
3. Squeeze that controller's **grip** and move your hand or turn your wrist.
   The ring turns **green** and the grabbed point follows the controller's aim.
4. While holding, use the **held hand's stick up/down** to bring the window
   closer/farther, and the **other hand's stick up/down** to grow/shrink it.
5. Release the grip to leave it anchored in space. You can grab it again.
   **A** confirms the entire edit; **B** restores the position, size and orientation from before
   opening Move window.

The panel retains its aspect. With **Face Viewer While Placing** on, it faces the head
only inside Move window; gripping holds that orientation until release. After
**A**, position and orientation remain anchored regardless of head movement. Hit
testing and the grabbed-point offset use the panel's actual rotation, including
tilt. It is not rotated freely or stretched with two hands. Normal editor limits apply.
If both controllers point at it, the hand making a fresh grip press wins over
an idle pointer. Grip uses 65% press / 35% release thresholds. A missed press,
an opening grip or a grip held while tracking returns cannot acquire the window;
release and press again while pointing at it.

Aim spaces are located in the session's LOCAL space at predicted display time.
Both position and orientation must be valid and tracked, with finite normalized
poses. Focus/tracking loss or a snapshot older than 250 ms releases the grab at
its current position. Input sampling stays on the render thread; the editor
consumes a snapshot on the UI thread. Touch grips temporarily stop emulating
LB/RB inside Move window, preventing an attempted grab from resizing the panel.
Regular gamepad/keyboard controls and the immersive camera editor retain their
existing behavior. Gameplay remains captured by the mod settings dialog.

The target ring is drawn in the existing color-transfer shader, using the
same panel coordinates in both eyes. It works with Sharp/Soft/Wide edges and
with color decoding enabled or disabled. It disappears outside Move window;
it does not point at or click retail-game menu items.

## Checks and remaining hardware tests

Build targets in a configured OpenXR build:

```powershell
cmake --build out/build/relwithdebinfo --target skate3 skate3-openxr-gamepad-test skate3-controller-driver-test skate3-window-grab-test skate3-vr-color-test skate3-openxr-probe
out/build/relwithdebinfo/src/vr/skate3-openxr-gamepad-test.exe
out/build/relwithdebinfo/skate3-controller-driver-test.exe
out/build/relwithdebinfo/src/vr/skate3-window-grab-test.exe
out/build/relwithdebinfo/skate3-vr-color-test.exe
out/build/relwithdebinfo/src/vr/skate3-openxr-probe.exe --controllers
```

The first test uses a fake OpenXR API and checks action creation/binding/attachment,
active/inactive sampling, focus loss and error cleanup, plus Xbox button masks,
analogue limits, non-finite inputs, D-pad routing and Back suppression. The second
uses the real ReXGlue input system and checks player index, endian conversion,
settings capture versus UI input, the settings chord and stale-snapshot release.
Both passed locally on 2026-11-03; the game and probe compiled successfully.

The motion update also passed: aim-space attachment/sampling and partial-setup
cleanup; ray bounds, back/parallel/invalid rays, rotated LOCAL frames, either
hand, no initial jump, wrist rotation, depth/size limits, release/reconnect
arming and cancellation; plus suppression of grip-to-shoulder mapping while
moving. The actual shader passed 26 RTX 4080 RGBA readbacks with Vulkan
validation and no reported messages, including blue/green indicators with
gamma decoding and feathering on/off. Existing stereo/editor tests passed.

The probe creates a short tracking-only session and logs controller values plus
tracked aim availability; it
does not show game images or prove playability. The local runtime returned
`XR_ERROR_FORM_FACTOR_UNAVAILABLE` (-35), so physical Touch gameplay has not
been verified. Remaining checks: both hands' buttons and analogue ranges,
pause/Back/D-pad chords, right-stick tricks, settings/editor operation, dashboard
focus changes, sleeping/waking a controller and all three VR modes.
Window-grab reach/comfort, marker visibility, resizing and release/apply/cancel
still need hands-on headset validation.
