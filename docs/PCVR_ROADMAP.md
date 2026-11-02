# Skate 3 PCVR — roadmap

Experimental VR adaptation of [Skate3Recomp](https://github.com/mchughalex/skate3recomp).
Native OpenXR stereo gameplay is experimental and has been viewed on Quest 3.
Several visual corrections remain under validation. Game data is NOT
distributed. Build notes: [PCVR_BUILD.md](PCVR_BUILD.md).

First playable checkpoint: **PCVR v0.1.0-alpha** (2026-11-02).
Verified behavior and remaining tests: [PCVR_V0_1_CHECKPOINT.md](PCVR_V0_1_CHECKPOINT.md).
Subsequent opt-in performance experiment: [PCVR_PERFORMANCE.md](PCVR_PERFORMANCE.md).

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
- [x] Windows build and Vulkan gameplay using local game assets.
- [x] Check PC OpenXR runtime and Quest Link headset tracking (standalone Vulkan session).
- [x] Hook Vulkan rendering into OpenXR swapchains.
- [x] Basic per-eye rendering and readable stereo HUD, confirmed on Quest 3.
- [x] Initial immersive third-person view with independent head orientation.
- [ ] Complete comfort limits, camera settings and broader immersive-mode tests.
- [ ] Implement stereoscopic window mode.
- [ ] Profile latency and frame time, target stable refresh.

## Dependencies
ReXGlue is pinned to the community ImGui compatibility fix in
https://github.com/mchughalex/rexglue-skate3/pull/18.

## Baseline investigation (2026-11-02)

- The supplied ZIP contains a `LIVE` Games on Demand header and 38 `Data`
  fragments. It is not an ISO and must be unpacked with a GOD-aware tool before
  using its game files. The ZIP itself must remain unchanged.
- Code generation requires the extracted `default.xex` and
  `data/webkit/EAWebkit.xex`. Their presence and compatibility inside this GOD
  package have not yet been verified. TU3 is recommended for code generation;
  the resulting TU3 build also requires its patches at runtime. See the build
  instructions in the README for the exact package name and configuration.
- The pinned ReXGlue commit is `d8ddb9f`; the ImGui submodule is checked out at
  `7038887`. Compilation has not yet verified the fix.
- The existing checkout has staged deletions in the nested `o1heap` submodule,
  including the source needed by the build. Preserve those changes and use an
  isolated checkout for compilation or resolve them with their owner.
- The Windows OpenXR active-runtime registry entry points to the Oculus runtime.
  This does not establish that a headset, Vulkan session, or tracking works.

## Implementation progress (2026-11-02)

- Extracted the subsequently supplied ISO into local `game/`: 103 files,
  6,404,940,920 bytes. Both required XEX files are present. The input ISO is
  unchanged. `cmake/ExtractGameIso.py` makes this preparation reproducible;
  four synthetic tests cover extraction, overwrite refusal, traversal, cycles
  and truncated extents.
- Installed CMake 4.4.4, Ninja 1.13.2, LLVM 23.1.4, Python 3.13.17 and Windows
  C++ development tools. Configured with Vulkan and TU3. ReXGlue and ImGui
  compiled; both XEX code-generation targets completed with exit status zero.
  Codegen emitted overlapping-boundary/unresolved-branch warnings for the main
  XEX; their full gameplay impact remains unverified.
- `out/build/relwithdebinfo/skate3.exe` compiled and linked successfully.
  Launched with `--gpu_backend=vulkan`, local game data and an isolated
  `out/runtime-test` user-data directory. `out/vulkan-baseline.log` confirms
  the RTX 4080 (Vulkan 1.4.357), a 1280x720 desktop swapchain, successful
  runtime initialization, TU3 application (3.0.0.0 -> 3.0.3.0), and native
  renderer registration. The process continued processing frames without
  logged error/critical messages in the inspected startup interval.
  Visual output and controller gameplay have not been verified: the Windows
  inspection plugin's `@oai/sky` runtime was unavailable.
- TU3 extraction succeeds and both patch SHA-256 hashes match the runtime's
  expected values. Packages and extracted files stay ignored by Git.
- Built and linked `skate3-openxr-probe` against the official OpenXR 1.1.54
  loader. Created a real OpenXR instance using `XR_KHR_vulkan_enable2` on
  the installed Oculus runtime. Initially `xrGetSystem` returned
  `XR_ERROR_FORM_FACTOR_UNAVAILABLE` (-35); enabling Quest Link resolved it.
- The OpenXR module owns instance cleanup, enumerates required extensions,
  queries stereo-view dimensions and Vulkan requirements when a headset is
  available, and reports failures with nonzero exit status.

## Milestone 2 — verified headset session (2026-11-02)

- Detected Meta Quest 3 through Oculus with recommended 2272x2464 images per eye.
- Runtime requirements report Vulkan 1.0 minimum / 1.2 highest tested. A real
  Vulkan 1.3 instance and device, created through `XR_KHR_vulkan_enable2`,
  successfully created a stereo session on the runtime-selected RTX 4080.
- Added `openxr_session.{h,cpp}` with dependency-ordered resource cleanup,
  session events, LOCAL/VIEW reference spaces, predicted frame pacing and
  head/eye pose location. Added `--track` to the native diagnostic executable.
- First ten-second hardware test: 901 frames, 901 valid 6DoF head poses,
  901 tracked head poses, 901 valid stereo pose pairs, 823 renderable frames.
  Display period: 11.111 ms (90 Hz); head translation varied by 0.1336 m on X.
  This validates tracking, not 90 FPS game rendering or GPU image submission.
## Experimental gameplay integration (2026-11-02)

- Shared runtime-selected ReXGlue Vulkan device/queue with OpenXR and created
  two 2272x2464 swapchains. Both eye views render from one immutable scene,
  with explicit GPU completion before swapchain release.
- The user confirmed the 3D world fuses in the headset. This does not validate
  complete camera behavior or performance across levels.
- Recovered visible, correctly proportioned frontend menus using an emulated
  menu image on a view-relative quad. The user confirmed improvement of the
  central HUD transform. A broad guest culling replacement and added native
  per-eye culling regressed the floor/NPC appearance and were removed; the user
  confirmed floor and NPC recovery. A later conservative expansion of the
  original planes, matched to the main perspective camera, improved rear-view
  visibility without those regressions according to the user. Complete
  visibility across levels remains under validation. The user subsequently
  confirmed that the pause menu is complete, centered and readable in the visor.
- Isolated the color-conversion black image: Meta's runtime images lacked
  STORAGE usage. Conversion now runs in an app-owned storage image followed
  by a transfer to XR. The user confirmed visible output and improved brightness.
  Conversion defaults on; Ctrl+F9 toggles it without restarting.
- Added a frontend movie timing guard for upstream issue #184. After rebuilding,
  the user confirmed progression beyond the difficulty selector. Local saves
  and the original assets were preserved.
- Vulkan validation exposed conflicting Meta/ReXGlue timeline feature nodes,
  a missing multisample-storage shader feature and later indexed draws beyond
  a streamed mesh's resident buffer. The device creation patch removes the
  feature errors; a common indexed-draw bounds check rejects stale draw ranges.
  A later failure without validation exposed missing queue synchronization
  between OpenXR calls and ReXGlue's desktop presenter. All four queue-touching
  XR calls now use the presenter's queue mutex. The next run continued past
  the former failure point into gameplay without validation.
- CPU timing logs include renderer submission and synchronous waits, not GPU
  timestamp profiling. Stable target refresh is not established.
- Ctrl+F10 toggles the conservative 200-game-unit culling expansion; it defaults
  on in VR after the above headset test. Synthetic checks cover both plane
  distance conventions, both inside signs, monotonic visibility expansion,
  unrelated cameras, orthographic volumes and invalid inputs. It rejects
  orthographic volumes and does not reconstruct streamed-out geometry.
- Investigating near-ground spikes reported only in the headset at speed.
  Added Ctrl+F7 source-image captures. Quest Link ASW was temporarily switched
  from auto to off; the user then reported no spikes with the queue fix active.
  Restored auto for comparison; the user confirmed that spikes remained absent
  while accelerating and also reported improved performance. ASW remains auto.
  No isolated ASW-causality claim is made.

Original project's history and credits are retained.

## Frame-work performance experiment (2026-11-02)

- Added optional per-eye GPU timestamps, sharing of the scene copy and
  light-space shadow passes, and an alternating 600-sample comparison.
- With the headset unavailable for wearing, measured actual GPU work using
  two identical desktop-camera renders. The run without Vulkan validation
  measured 3.736 -> 3.240 ms CPU render work (13.3%) and 1.756 -> 1.702 ms GPU
  render work (3.0%), with balanced scene-item counts across variants.
- The validation-enabled run completed 600 samples without VUID messages;
  stereo/culling/timestamp tests and SDK patch tests pass.
- `skate3_vr_share_frame_work` remains off by default. Headset visual checks
  and full frame-rate/latency measurements remain pending; no VR FPS gain is
  claimed from the desktop workload.
