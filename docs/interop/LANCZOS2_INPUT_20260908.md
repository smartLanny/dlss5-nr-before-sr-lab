# #84 optional scale-aware Lanczos-2 input

This isolated slice starts at dev6/#172 `987ef37` and targets only `next/0.5`.
Production source is `704fcf838d22a0d878e64ce822cfe84269bfb67c`, beta0.5-dev7 /
PE 0.5.0.7. This checkpoint is source/compilation evidence; GPU and complete
DLL linking have not yet run. The previous dev5/dev6 binaries and captures
remain in their original separate worktrees.

## Implemented behavior

`NRInputFilter=0` remains ExactArea. Value 1 selects optional Lanczos2AA only
for the existing pre-SR route. R/N/O, guide interpretation, original SR and
Feature18's N-to-N input/output remain unchanged. The new passes change the
proxy before the existing single NR call; they do not consume any old Neural.
The identity-original-color mode and full 1:1 grid keep the existing Area path.

For each axis `s=N/R`, the public mathematical weight is
`sinc(s*d) * sinc(s*d/2)` for `abs(s*d)<2`, with explicit centre handling.
Source-space support therefore expands to `2/s`. Weights are normalized for
every destination pixel, including clamped border taps. The centre is
`(destination+0.5)/s-0.5`; the source loader applies the active rect's offset
and clamps within its extent, not within unrelated allocation padding.
Supported per-axis ratios are 1/8 through 1, with a bounded loop support;
outside this optional range the original Area path keeps the requested N.

The horizontal pass owns an Nwidth x Rheight RGBA16F texture. It stores signed
RGB and an exact 0/1 validity alpha. A reversible four-times storage scale uses
the same existing colour/exposure decode contract in both passes: bounded
domains use 4; automatic HDR uses four times canonical source normalization;
PQ uses four times its existing PQ-to-paper-white scale. This prevents ordinary
HDR unit scaling from overflowing half storage without saturating negative
lobes. FP16 precision/underflow remains a measurable tradeoff.

The vertical pass reads this signed intermediate, normalizes its own weights,
restores the scale, and only then applies the existing proxy curve and final
bounded model encoding. Invalid/overflow intermediate support falls back to
the Area calculation; nonfinite final proxy data becomes finite black. The
Area helper was factored without changing valid arithmetic. Its encoded 1:1
and final proxy paths additionally guard nonfinite values; malformed floating
data is not promised bitwise equality with previously undefined model input.
Legal finite Area and 1:1 values remain the comparison baseline.

ModelEncodedValueDomain stays in its explicitly declared stored-value domain.
Linear, sRGB, automatic HDR and PQ keep their current decode/encode helpers;
this does not grant an unknown HDR contract or reinterpret colour primaries.
No upstream Magpie/GPL implementation was copied; the kernel follows the public
sinc/window mathematical definition independently.

## Resource, history and integration contract

- Optional horizontal/vertical PSOs do not poison the base codec on failure.
  The intermediate requires typed FP16 ShaderLoad/UAV store support.
- New or resized descriptors require `CanMutateSlotLocked`. The intermediate
  belongs to its existing slot and retires with its exact fence/recorded-list
  dependencies; no new unbounded context/resource pool exists.
- Driver-reported allocated bytes, including alignment and temporary resize
  overlap, are bounded by 64 MiB per texture and 256 MiB total Lanczos textures
  per Core. This quota is separate from the existing EditField quota; it is
  not a claim about all driver/PSO memory.
- Persistent allocation/format/PSO failure retains Area for that context,
  avoiding rotating slots that alternate kernels and repeatedly reset history.
  Requested filter changes participate in existing history/config identity and
  retire the old temporal context once. Busy slots can temporarily retain Area.
  Actual filter/scale changes use existing reset_retry_pending; only successful
  NR Evaluate consumes that Reset. No every-frame feature recreation is added.
- Recording order is horizontal -> UAV barrier -> intermediate SRV -> vertical
  -> existing proxy barrier/SRV -> the existing NR invocation. No input is
  substituted half way through a failed preparation.
- Actual Lanczos input recording is codec bit **65536**. Per integration
  coordination, the input plane uses **t7/u7**, total SRV/UAV counts **8/8**;
  t6/u6 are explicit R16G16_FLOAT typed-null reserved slots for separately
  validated #85, with no #85 shader/algorithm imported here. Root constants
  remain 35. Bits 16384 and 32768 remain available to #85/#86 respectively.
- UICorrection's old unsigned-only claim was corrected as instructed by the
  coordinating task's runtime getter evidence. The actual SetUInt behavior
  and #162's unverified visual outcome are unchanged.

## Minimum checks completed

Clean pinned generation and all original manifest hashes passed. Generated Core
C++ frontend and all nine production SM5 entries compiled. The extracted
production weight helper passed 21,706 scalar symmetry/support/normalization/
constant/boundary/signed-lobe checks. The initial scalar endpoint oracle expected
bit-exact zero after floating reciprocal/multiply; it was corrected to a 1e-10
endpoint tolerance plus exact zero outside support. The initial failure log is
retained. Existing shader exposure/ReShade attribute warnings were not hidden.

Generated Core SHA256:
`ea0006faaa583e108f8cfdeba5f9773cab0a3552f9f4bf8a03653e33ebe0ce26`.
Source-manifest SHA256:
`65fcffd43ed3ddf09685fc8d0494a79ca2b52a6a25b0c2d71f6cd2ce648c6ca0`.
Compile-only input fixture executable SHA256:
`9215109c78f1563cd3bced1ad72424052076e541d49b7d6c6bb898d2357b3942`.
Private logs and `build/lanczos-minimal-validation/validation.json` preserve
these identities. Neither this compile nor the planned fixture is NR output,
game quality, performance or live allocation/Reset/DRS fault validation.

## Coordinated next check

The compiled input-only fixture is 200 small 31x23 pairs: five explicit value
domains, constant/impulse/step/checker/1px-line/ramp/NaN/Inf, and requested
50/67/75/85/100% ratios with actual integer N recorded. It uses real signed FP16
intermediate/proxy resources and nonzero source rect/padding. A frozen dev6
shader supplies the legal-finite Area reference; the old full gate is not run.
The 1:1 candidate bypasses intermediate allocation and executes Area directly.

After the coordinating task releases the GPU, use a new output directory:

```powershell
./scripts/validate-codec-gpu.ps1 -ToolchainRoot $Toolchain `
  -SourceRoot "$Generated/core" -InputFilterOutput $NewCapture `
  -BaselineSource "$FrozenDev6/core/src/nr_before_sr.cpp"
python scripts/validate-input-filter.py --capture $NewCapture `
  --source "$Generated/core/src/nr_before_sr.cpp" --negative-control coordinate
python scripts/validate-input-filter.py --capture $NewCapture `
  --source "$Generated/core/src/nr_before_sr.cpp" --negative-control normalization
```

These commands have not executed at this checkpoint. Raw source, intermediate
and model-input proxies will be read only after real queue completion within
8 MiB. Python independently checks both axes, Area, coordinates, normalization,
FP16 signed lobes, finite model bounds, alpha, padding and 1:1 equality. It will
explicitly record zero NR/SR calls and no Neural plane/reuse. Genuine new-kernel
model output requires a fresh actual NR run with controlled history.

Remaining work: the coordinated shader/resource matrix, full DLL link, live
slot/DRS/Reset/allocation-failure controls, signed-HDR/underflow/overshoot cost
measurement and actual NR/game sequences. This implementation adds a pass,
signed storage and sinc arithmetic; no net speedup or default-policy promotion
is claimed. Other #85/#86 algorithms remain separate until reviewed integration.
