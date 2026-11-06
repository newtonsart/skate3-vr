# Loading diagnostics: speculative meshes and XMA block ends

The November 4 capture contains 183 first-chance guest access-violation
messages on native prewarm workers: three addresses retried 61 times each.
The game continues because `GuestTryCopy` rejects the reads through its
fault guard. A later user run shows the same 61-attempt pattern with other
addresses, including aligned ones; alignment alone cannot validate them.

The worker now checks the guest allocation metadata for the mesh record,
vertex descriptor and buffer-object headers before calling the shared mesh
builder. Unreadable resources follow the existing deferred-retry path. The
checks use the runtime heap metadata rather than the OS `VirtualQuery` path,
which has caused VAD-lock stalls in render-time decoding. Normal render and
capture reads retain their existing path. The guarded copies remain in place
because streaming can revoke a resource after the allocation check.

The old XMA decoder also produced `input offset 16416 exceeds buffer size
16384`. Its frame-advance path could store a next-packet cursor beyond the
current input block, then report that cursor as an error on the next pass.
The pinned SDK patch now completes or loops the block before storing such a
cursor, validates the incoming cursor before seeking the bitstream, and avoids
installing an out-of-range first-frame offset after swapping buffers. Actual
invalid incoming offsets still report errors. The old decoder remains
selected; the frame/subframe audio algorithm has not been replaced.

Validation:

```text
python cmake/test_loading_guards.py
python cmake/test_openxr_interop.py
cmake --build --preset relwithdebinfo --target skate3 --parallel 6
```

All passed on Windows. The focused regression harness compiles the actual
allocation-check, worker-validation and XMA boundary code with small fixtures.
It covers the logged invalid pointers, unreadable spans, arithmetic overflow,
temporarily unavailable metadata, block ends, streaming, loops, full output
rings and the retention of error reporting for a genuinely invalid cursor.
The SDK patch also passes the existing repeat-application checks.

These are build and regression results, not a new gameplay or listening test.
The currently tested root executable was left unchanged. A separate candidate
is staged at `out/loading-fix-candidate-20261104/Skate3-VR.cmd` in the outer
workspace. It uses the existing game data and save profile, with tracing
enabled. Do not run it concurrently with the current game. After the current
test ends, loading a map and playing with audio will establish whether the
specific messages disappear and whether any other source remains.

Checks of allocation metadata cannot prevent every race with streaming, and
these changes do not address the initial resource-preparation stall. No claim
of zero access faults, shorter loading times or audibly improved sound has
been established yet.


## Remaining fingerprint read (2026-11-04 evening)

The later user session still reported 122 access violations, 61 each at
0x0507C648 and 0x007A8598. A temporary stack probe located both in
`ComputeItemFingerprint`, called by `BuildItemFromMesh` on prewarm workers.
The buffer objects were readable, but their payload pointers were not fixed
up yet. Checking after the builder returned was too late: it samples the
payload itself. The shared builder now checks the complete vertex/index
spans and index-byte-count overflow before computing the fingerprint.
Existing deferred retries and guarded copies remain. The cache-hit path
keeps its existing checks and fingerprint cadence.

The focused regression harness now checks the actual builder condition,
including both logged addresses, truncated spans and overflow, and asserts
that fingerprinting is never entered for these inputs. The temporary stack
probe was removed from the final build. The final isolated OpenXR validation
run completed loading and over 800 scene generations with zero access
violations, zero error-level messages and zero VUID messages, with
VK_LAYER_KHRONOS_validation explicitly enabled. This covers one boot/load,
not every streaming race or map. The final build is installed into both
launch locations; the candidate is no longer the only place with these fixes.
