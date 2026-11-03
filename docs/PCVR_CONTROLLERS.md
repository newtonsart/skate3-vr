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

This does not add tracked hands, pointing rays, motion-based tricks or Touch
vibration. Keep a regular gamepad available during this experimental stage.
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

## Checks and remaining hardware tests

Build targets in a configured OpenXR build:

```powershell
cmake --build out/build/relwithdebinfo --target skate3 skate3-openxr-gamepad-test skate3-controller-driver-test skate3-openxr-probe
out/build/relwithdebinfo/src/vr/skate3-openxr-gamepad-test.exe
out/build/relwithdebinfo/skate3-controller-driver-test.exe
out/build/relwithdebinfo/src/vr/skate3-openxr-probe.exe --controllers
```

The first test uses a fake OpenXR API and checks action creation/binding/attachment,
active/inactive sampling, focus loss and error cleanup, plus Xbox button masks,
analogue limits, non-finite inputs, D-pad routing and Back suppression. The second
uses the real ReXGlue input system and checks player index, endian conversion,
settings capture versus UI input, the settings chord and stale-snapshot release.
Both passed locally on 2026-11-03; the game and probe compiled successfully.

The probe creates a short tracking-only session and logs controller values; it
does not show game images or prove playability. The local runtime returned
`XR_ERROR_FORM_FACTOR_UNAVAILABLE` (-35), so physical Touch gameplay has not
been verified. Remaining checks: both hands' buttons and analogue ranges,
pause/Back/D-pad chords, right-stick tricks, settings/editor operation, dashboard
focus changes, sleeping/waking a controller and all three VR modes.
