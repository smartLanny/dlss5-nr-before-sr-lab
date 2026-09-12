# D16 RR edit carry experiment — implementation slice, NOT game-ready

Base: `3c13f69119ddb0a0c1246f0348eb15c8aac64f76` (D16).

## Actual delivery
Three HLSL compute entries, shared C++/HLSL scalar algorithms, exact seam/history
receipts, cbuffer layout assertions and executable host tests. NOT connected to
D16 model dispatch, GUI, resource allocation or command recording. An INI key
cannot enable this feature yet. No placeholder option is advertised as functional.
ROG MCP access was denied by the platform. No Windows/SM5/WARP/NVIDIA/game run.

Reference, not copied runtime:
https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/e237f895623742b761f9e5f00067cb3dc62619f4/docs/RESIDUAL-ACROSS-RR.md
Upstream PR #26 explicitly lacks depth disocclusion and game quality/performance
validation. Its constant temporal blend does not prove all residual noise is unbiased.

## Implemented kernels
- PrepareGuidesCS: canonical current->previous pixel motion; hardware depth to
  positive view-Z; expected previous-camera depth from a verified per-pixel input
  or a verified current-clip->previous-clip matrix. Camera-only prediction is
  rejected on disagreement with native motion (independently moving objects).
- AccumulateCS: signed exposure-neutral edit, depth-tested bilinear history taps,
  current 3x3 edit-neighborhood clamp, confidence-dependent response. A cold frame
  does not read uninitialized history.
- ComposeCS: depth-aware signed R->O reconstruction after clean RR, preserving
  output alpha and signed scene-linear values. No second NR at O, no extra SR.

E_R must be the existing Core's composed NR image, not a raw model-domain image.
Delta=(E_R-X_R)/preExposure. Existing strength/colour/skin controls are already in
E_R and must not be applied twice. All writes target private resources.

## Depth is not just two texture samples
Compare expected previous-camera depth with stored previous depth, not current
and previous raw depths at the same screen coordinate. Current depth and MV alone
do not supply previous camera transforms or moving-object previous positions.
Without a verified matrix/provider, do not set CameraMappingVerified or fabricate
an identity camera. History must reset/disable; a weaker MV-only variant would
need a separately labelled, measured experiment.
Normalize native subrect, depth direction, motion units and raster/jitter phase
before binding the owned zero-origin R textures. Keep the last actually accepted
history frame and its matching motion-reference source, not the last capture.

## Remaining D16 integration
The inspected ProcessEvaluate contains a native Feature13 pass-through. Do not
assume archived 0.4 RR notes describe this exact source. DispatchEvaluate already
owns a source EvaluateScope; add an opt-in classified RR handler, not fake Feature1.

1. Get a real source claim and same-list/queue/active-rect/colour proof. Snapshot
   original X_R to owned storage. Reuse current D16 real NR1/NR2, independent
   histories, N1/N2, profiles and matched-residual reconstruction to produce E_R.
2. Extract the private-image part of PrepareBeforeSr without its game-parameter
   substitution. No transient shared g_config/feature_id/Color mutation.
3. Prepare/accumulate the edit with a pinned previous edit/depth pair. Invoke
   original RR exactly once with unchanged params/input. Consume the edit only
   at the successful same-frame post boundary; use existing command-state scopes.
4. Compose into private O, apply existing final headroom/gamut policy once in the
   correct domain, then perform legal copyback. Before-recording failure keeps
   clean RR. Opaque failure/partial recording follows existing quarantine, never
   blind retry or a second original RR Evaluate.
5. Use D16 immutable per-dispatch descriptors and exact recording/fence retirement
   for all histories/params/surfaces. Fixed ring depth or CPU counts are not proof.
   SameFramePair is a seam ticket, not a replacement for Core source claims; an
   already claimed source cannot be re-armed and processed twice.
6. Keep Deferred and Present mutually exclusive NR owners for the same source.
   Off means no added allocation/copy/model work. Retain verified default routes.

First vertical slice: D3D12, one proven view, same direct queue, canonical FP16
scene-linear colour with known exposure. These are capability contracts, not an
EXE whitelist. R8/sRGB/PQ require existing boundary codecs, not changed format tags.
Use RGBA32_FLOAT for edit history/reprojection, R32_FLOAT for view depth. Cbuffer
is 144 bytes, matrix offset64, flags offset128. Every optional descriptor must be
valid even when its verification flag is zero. Reflection, real GPU barriers,
allocation, resize and all-consumer retirement remain untested.

## Actually executed
GCC warnings-as-errors and Clang ASan/UBSan: shared scalar sign/depth/NaN/camera-
depth tests plus 20,000 randomized cases. Frame tests cover 15 key mismatches,
one-shot consumption, native/pre failure, exact history reference across gaps,
wrong fence/queue, unsent/poisoned work and UINT64_MAX rejection. C++ cbuffer size
and offset assertions pass. These are CPU tests, not GPU shader/game proof.

Windows continuation uses the existing compiler built by scripts/build.ps1:
```
python scripts/validate-d16-edit-experiment.py --mode rr --cxx <compiler> --shader-compiler <existing-codec-compiler>
```
The script calls the actual D16 compile-codec-artifact.exe SM5 CLI after flattening
only the reviewed local include. Without that compiler it reports SM5=not-run.
Run reflection/WARP next, then real private-image NR and same-frame RR injection.
No original Core shader blobs or model DLLs need to be replaced for kernel tests.

## Fair performance/quality test
RR itself is unchanged. At equal NR N, this is NOT automatically cheaper than
reduced post-RR NR at N. Compare clean RR, full post-NR reference, reduced post-NR
at N, and this carry at N. Fix R/O, model/style/pass count/strength and scene.
Record RR, NR1/NR2, guide prep, accumulation, compose/copy, total source P95/P99 and
VRAM; inspect movement, occlusion, thin geometry, faces, highlights and reflections.
FG off first. A low-resolution model sees noisy pre-RR colour, not the clean
post-RR image; depth tests cannot recreate input information it never received.
No FPS or quality gain is claimed. Track under #15; do not close it from this drop.
