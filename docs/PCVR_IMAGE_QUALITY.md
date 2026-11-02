# Eye render resolution and clarity

The previous VR path rendered both eye views into the desktop-sized guest output
at 2560x1440 on this PC, then linearly resampled each into a 2272x2464 XR image.
In particular, 1440 source rows were stretched to 2464 rows. This loses detail
before the headset's own composition/transmission stages.

The new optional path sizes the source render target from the runtime's
recommended eye dimensions. It changes actual native scene rasterization,
not just the size of the final XR image. The swapchains keep their runtime
recommendation; window/HUD/menu proportions use logical display dimensions.

## Controls and cost

Open **RB + Start → Video → VR Render Resolution** (or F1 on the PC).

| Choice | Source dimensions on the tested Quest 3 | Source pixels versus previous 2560x1440 |
| --- | --- | --- |
| Desktop (compatibility) | Existing desktop render dimensions | 1.00x |
| HMD 75% | 1704x1848 | 0.85x |
| HMD 100% | 2272x2464 | 1.52x |
| HMD 125% | 2840x3080 | 2.37x |

Scale is applied to both dimensions. Pixel counts are not frame-time predictions.
Recommended sizes can differ with the headset and runtime configuration. If
the eyes report different sizes, the source uses the maximum width and height.

HMD 100% is the first setting to compare. The selection applies on the next
source frame and is saved in the existing settings file. Command-line equivalents
are `--skate3_vr_render_scale=0`, `0.75`, `1.0` and `1.25`. Zero remains the build
default for compatibility; the user's saved HMD 100% selection survives restart.
Larger scales have a GPU and memory cost; use 75% or Desktop if needed.

Auto shadow quality is deliberately based on the source height **before** the
VR override. Without this separation, increasing eye height from 1440 to 2464
also increased the automatic shadow tile from 1024 to 2048, quadrupling each
tile's pixels. Explicit shadow settings and shared light-space work are retained.

The change does not replace retail textures or change Meta's Link settings.
Runtime upscaling/sharpening can also affect final presentation; see
[Meta's explanation of image scaling and Link sharpening](https://developers.meta.com/vr/blog/vr-image-quality-meta-quest-super-resolution/).
This investigation does not isolate headset optics or transmission losses.

## Implementation and checks

The reproducible SDK patch provides an atomic output-size override immediately
before allocation/refresh of the presenter's guest output. Width/height change
together. It also retains the original source height for automatic shadow sizing.
The game updates this override after OpenXR detection and when the live scale
changes; shutdown clears it. Ordinary desktop runs retain their previous sizes.
The eye matrices, color decode, queue locking and image release waits are unchanged.

Local checks on 2026-11-02:

- Windows game and stereo/math targets compiled. Tests cover the supported
  render extents, invalid scales/sizes and logical menu aspect independent of
  the tall eye raster. Movie and SDK patch tests passed.
- A desktop diagnostic rendered actual game content at 2272x2464 with the
  Khronos validation layer enabled and no VUID messages. It was a single-camera
  source-resolution check, not a headset or stereo performance benchmark.
- The user used wired Quest Link, switched the resolution setting and reported
  a very large clarity improvement. Their saved scale is 1.0. The session log
  records live switches through 75% and 100%, followed by more than 8000 stereo
  submissions without a logged stereo-rendering failure.
- The updated build was reopened with the same user profile and scale 1.0 after
  the user's approval, to retain eye quality while restoring prior auto shadows.
  Its log confirms actual native gameplay at 2272x2464, a 1024x1024 automatic
  shadow tile and continued stereo submissions after restart.
  Broader headset/performance validation of the final combination remains needed.

For a standalone size diagnostic, use a separate profile with
`--skate3_vr=false --skate3_vr_resolution_test=true --skate3_vr_render_scale=1`.
It uses the previously recorded 2272x2464 recommendation, labels itself desktop
only and creates no XR session. Do not count this path as headset validation.
Earlier XMA buffer errors and shutdown heap-release errors also occur in the
diagnostic logs; they are separate from the Vulkan resolution check.
