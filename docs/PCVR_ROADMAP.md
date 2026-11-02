# Skate 3 PCVR — roadmap

Experimental VR adaptation of [Skate3Recomp](https://github.com/mchughalex/skate3recomp).
No working VR code is present yet. Game data is NOT distributed.

## Target
Meta Quest 3 connected to a Windows PC (RTX 4080); standard Xbox/PlayStation
controller for all gameplay, head tracking from the headset via OpenXR.

## VR modes
- **Immersive VR**: a stereoscopic, full-scale 3D world around the viewer,
  tracking the skater in third person with independent head orientation.
- **Stereoscopic window**: a fixed virtual screen displaying the game in stereo,
  with real binocular depth and no head-driven in-game camera.

Both modes share one OpenXR/Vulkan core; no Touch-controller gameplay planned.

## Milestones
- [ ] Clean Windows build and launch on Vulkan using legally obtained game assets.
- [ ] Check PC OpenXR runtime and Quest Link headset tracking.
- [ ] Hook Vulkan rendering into OpenXR swapchains.
- [ ] Render two correct eye views and maintain stereo HUD readability.
- [ ] Implement immersive third-person VR mode.
- [ ] Implement stereoscopic window mode.
- [ ] Profile latency and frame time, target stable refresh.

## Dependencies
ReXGlue is pinned to the community ImGui compatibility fix in
https://github.com/mchughalex/rexglue-skate3/pull/18.

Original project's history and credits are retained.
