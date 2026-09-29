# Private SR guide producer: bounded initial contract

`src/nr_private_sr_guides.h/.cpp` wraps the existing `NormalizeGuides`
kernel. It does not allocate textures, call NGX, install hooks or own submission.
The caller supplies an `nr_edit_dx12::Kernels` owner, a recording lease, native
read-boundary inputs and distinct exact N-sized R32_FLOAT depth / R32G32_FLOAT
motion outputs. Outputs use zero-origin active rectangles; depth stays in the
native normalized device-depth convention, including reversed depth.

## Capture before native SR, confirm after native SR

1. At the verified native Feature1 read boundary, retain the original source,
   recording identity, active guide rectangles, original feature flags, original
   MV.Scale and raw native jitter values. Use `capture.game_mv_*` and raw jitter
   evidence, not the already transformed `signature.motion_scale_*`, fallback 1,
   or the user's NR jitter-forwarding setting.
2. `Prepare(Input)` checks the contract without GPU calls. Disabled returns
   immediately. `Record` reads the original guides before the opaque native
   Evaluate can mutate them; the normalized textures are the same-source owned
   snapshots. The caller must restore its native command state around Record.
3. Even if native Evaluate or later B/Z production fails, retain every recorded
   guide packet and its inputs/outputs until exact submission plus recording-end
   proof. Forward submission/discard to the **guide kernel owner** independently
   of whether the private SR consumer was reached. Do not drop leases on a failed
   Finish.
4. After successful native Feature1 and same-source B/Z production, pass their
   explicit source and recording identities to `Finish`. Only successful Finish
   returns `post_sr_phase_verified`. Neither reaching a post hook nor observing
   an output pointer proves that phase.

## Proven and unsupported phase cases

The first supported contract is SDR, native Feature1, known original flags,
non-jittered motion vectors and **known exactly zero original input jitter**.
`JitterUnknown` and `NonzeroJitter` are distinct refusal reasons. Typical
temporally jittered game frames are not supported by this initial producer.

The non-jittered-MV creation flag means displacement does not include jitter.
It does not prove that the spatial locations at which depth and motion are
stored are already aligned to the post-SR raster. The current shader samples
nearest centers without a phase shift. Nonzero jitter would require a proven
spatial phase transform, its sign and render/output scale, matching depth and
motion sample conventions, and handling visibility near discontinuities.
Those facts are not supplied by `NrGuideSignature`; simply forwarding
`post_sr_phase_verified=true` does not establish them.

Native low-resolution MV uses the R active extent; high-resolution MV uses O.
Spatial sample positions use the respective active rect, ignoring allocation
padding. In both cases the original NGX MV.Scale converts stored vectors to
native render-pixel displacement; the additional componentwise factor is N/R,
not N/MV-allocation-size. Output MV.Scale is consequently 1. Non-finite motion
samples map to finite out-of-bounds displacement, never stationary zero.

Rejected cases include native RR, HDR/unreviewed/jittered-MV flags, missing scale,
invalid/overflowing extents, unproven recording/source correspondence, skipped
motion-reference sequence without reset, linear/projected/placeholder depth,
and depth inversion overridden away from the native flag.

## Focused verification

Run `scripts/validate-private-sr-guides.py --compiler <clang++.exe> --output
<fresh-directory>`. CPU checks exercise evidence failures, flag gates, source
and recording changes, zero/nonzero/unknown jitter, signed anisotropic units,
subrect bounds, depth conventions and post-native Finish refusal. WARP checks
actual atlas-offset sampling, low/high-resolution MV units, reversed depth,
NaN handling, output shape/type refusal and disabled no-dispatch behavior.

This is producer CPU/WARP validation, not private Feature1 inference, complete
Core integration, game acceptance, motion quality or performance validation.
