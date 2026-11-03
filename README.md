<picture>
  <source media="(prefers-color-scheme: dark)" srcset="banner.png">
  <source media="(prefers-color-scheme: light)" srcset="banner-light.png">
  <img alt="Skate 3 VR" src="banner-light.png">
</picture>

# Skate 3 VR

Play **Skate 3 in virtual reality**, with stereoscopic 3D, head tracking and
your regular gamepad. Based on [Skate3Recomp](https://github.com/mchughalex/skate3recomp).

**Experimental build:** tested on Windows with Quest 3 over wired Link.
Other OpenXR headsets have not been tested yet. Game files are not included:
you need your own Xbox 360 copy of Skate 3.

## Getting started

1. Connect your headset to the PC and start its **PCVR session**. On Quest,
   enter **Quest Link** and select Meta Link as the active OpenXR runtime.
2. Connect an **Xbox or PlayStation gamepad**.
3. Double-click **[Skate3-VR.cmd](Skate3-VR.cmd)** in the project folder.
4. Choose a mode in the launcher:

| Button | What you see |
| --- | --- |
| **Immersive VR** | The game world around you, with independent head tracking. |
| **3D window** | A floating screen with the desktop framing and stereoscopic depth. |
| **3D window + passthrough** | The 3D screen over a view of your room, when supported by Meta Link. |

**No launch commands needed.** The launcher uses the VR build in
`out/build/relwithdebinfo` and the extracted game in `game/`.
If either is missing, it displays an error: follow the [VR setup and build guide](docs/PCVR_BUILD.md#build-the-openxr-game-on-windows).
Cloning this repository does not automatically install or build the game.

## Controls and settings

Gameplay uses the original gamepad controls; Touch controllers are not supported.

| Input | Action |
| --- | --- |
| **Start** | Pause the game. |
| **RB + Start** | Open mod settings, also visible in the headset. |
| **F1 / Escape** | Open settings using the keyboard. |
| **Ctrl + F8** | Recenter the view or place the window in front of you. |
| **Ctrl + F11** | Switch between the three modes while playing. |

Under **RB + Start → Video**, adjust the VR mode, image quality, window size
and position, camera and HUD. Settings are saved.
If your PlayStation gamepad does not respond, select **Controls → Controller
Backend → SDL** and restart. [More about settings and camera](docs/PCVR_SETTINGS.md).

In window modes, select **Video → Move window**. The left stick moves it
left/right and up/down; the right stick moves it closer/farther; **LB/RB** resize
it. Press **A** to place it or **B** to restore its previous placement.
In immersive mode, **Video → Adjust camera** keeps the world visible while
the left stick changes camera height and the right stick changes distance.
Press **A** to apply or **B** to cancel. Both editors use a small panel below
the scene and reserve the gamepad controls until you return to settings.

## Image quality

Start with **Video → VR Render Resolution → HMD 100%**.

- **HMD 75%:** lower GPU workload.
- **HMD 100%:** render at the headset's recommended resolution.
- **HMD 125%:** additional supersampling at a higher GPU cost.
- **Desktop (compatibility):** use the desktop render resolution.

The launcher keeps your quality preferences and disables MSAA to match the
tested VR configuration. Original game textures remain unchanged.
[Image-quality details](docs/PCVR_IMAGE_QUALITY.md).

## Passthrough: see your room

In the Meta Link PC app, open **Settings → Developer** (or **Beta**):

1. Enable **Developer runtime features**.
2. Enable **Pass-through over Meta Quest/Horizon Link** and accept Meta's notice.
3. Enter Quest Link and relaunch the game with **3D window + passthrough**.

If Meta does not expose passthrough, the window uses a black background.
**The camera background still needs headset validation in this project.**
[Passthrough setup and diagnostics](docs/PCVR_PASSTHROUGH.md).

## Saves and settings

The launcher uses the existing VR test profile in **`out/runtime-test/`**.
Your profile, saves and settings are kept there; preserve this folder when updating.
If you previously used a different user-data folder, the launcher does not migrate
those saves. Running `skate3.exe` directly may use a different profile folder.

## Troubleshooting

- **Missing build or game files:** prepare the OpenXR-enabled build, game files
  and title update using the [VR guide](docs/PCVR_BUILD.md).
- **No game image in the headset:** enter your PCVR session before launching.
  On Quest, check that Meta Link is the active OpenXR runtime.
- **The window is out of view:** look forward and press **Ctrl + F8**.
- **The pause menu looks wrong:** try **Video → VR Pause View → Flat panel**.

The 3D window recently received a framing fix and needs another headset check.
Broader testing of maps, editors and performance is still pending;
72/90 FPS throughout the game is not guaranteed.

## Development and credits

VR development is on **`main`** in [skate3-vr](https://github.com/newtonsart/skate3-vr).

- [Building and OpenXR diagnostics](docs/PCVR_BUILD.md)
- [Performance and measurements](docs/PCVR_PERFORMANCE.md)
- [Project status and remaining work](docs/PCVR_ROADMAP.md)
- [Base-project desktop, DLC and build documentation](docs/DESKTOP.md)

Credit to [Skate3Recomp and its contributors](https://github.com/mchughalex/skate3recomp),
the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) and
[Xenia](https://github.com/xenia-project/xenia), whose research and tooling
have contributed to the recompilation ecosystem.
