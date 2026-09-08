# #84 optional scale-aware Lanczos-2 input

This isolated slice starts at dev6/#172 `987ef37` and targets only `next/0.5`.
Final production source is `266b25d23ed66538b95e4e0eac66425574466711`, beta0.5-dev7 /
PE 0.5.0.7. One 200-pair input-only WARP batch and complete bilingual Core/nrchain
linking have now run, as recorded below. The previous dev5/dev6 binaries and captures
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
  overlap, are bounded by 64 MiB per slot and 256 MiB total Lanczos textures
  per Core. This quota is separate from the existing EditField quota; it is
  not a claim about all driver/PSO memory. Source 266b25d explicitly checks
  `newBytes <= 64MiB - slotOldBytes` while retaining the old plane until a
  successful replacement; the global check likewise includes the old plane.
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

## Initial compilation checkpoint

At source `704fcf838d22a0d878e64ce822cfe84269bfb67c`, clean pinned generation and
all original manifest hashes passed. Generated Core
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

## Final source and one coordinated WARP batch

The compiled input-only fixture is 200 small 31x23 pairs: five explicit value
domains, constant/impulse/step/checker/1px-line/ramp/NaN/Inf, and requested
50/67/75/85/100% ratios with actual integer N recorded. It uses real signed FP16
intermediate/proxy resources and nonzero source rect/padding. A frozen dev6
shader supplies the legal-finite Area reference; the old full gate is not run.
The 1:1 candidate bypasses intermediate allocation and executes Area directly.

After the slot replacement peak correction, source 266b25d was pinned and
generated into private `build/dev7-budget-final`; generated C++ passed again.
Its generated Core SHA256 is
`c0133f6abe79a56b6dc37f11abcf2ddb3cbdee173f7a8d0e676188777a6c9d3c`;
source-manifest SHA256 is
`482b1223363950be3dbeb69c7ccbdcdfc438f840b3b4849a115fa1ef85f255ae`.
All 103 original manifest entries matched before and after full linking.

The coordinating task allocated one GPU window. These commands ran with
`$Generated=build/dev7-budget-final`, `$NewCapture=build/input-filter-warp-final`,
and `$FrozenDev6` pointing to the preserved dev6-generated stage:

```powershell
./scripts/validate-codec-gpu.ps1 -ToolchainRoot $Toolchain `
  -SourceRoot "$Generated/core" -InputFilterOutput $NewCapture `
  -BaselineSource "$FrozenDev6/core/src/nr_before_sr.cpp"
python scripts/validate-input-filter.py --capture $NewCapture `
  --source "$Generated/core/src/nr_before_sr.cpp" --negative-control coordinate
python scripts/validate-input-filter.py --capture $NewCapture `
  --source "$Generated/core/src/nr_before_sr.cpp" --negative-control normalization
```

The GPU batch completed all 200 pairs, 960 raw planes and 6,319,040 bytes,
below its 8 MiB capture limit. GPU fixture commit was clean source 266b25d.
The initial CPU oracle then rejected case 31's validity: its double-precision
support calculation assigned an invalid endpoint tap weight of about
`-3.7391620545967533e-31`, while the actual FP32 support branch excluded that
tap. The initial failure remains in `input-filter-warp.log`; no GPU retry or
raw replacement occurred. Validator `7f6644f16f29650e74d6ea5949a80502afea6313`
now mirrors FP32 scale, centre, support endpoint and arithmetic decisions.
It does not discard nonzero weights through an arbitrary epsilon. Only the
same raw data was rechecked, using:

```powershell
python scripts/validate-input-filter.py --capture build/input-filter-warp-final `
  --source build/dev7-budget-final/core/src/nr_before_sr.cpp `
  --gpu-fixture-commit 266b25d23ed66538b95e4e0eac66425574466711
```

The CPU recheck passed 85,240 finite bounded model pixels, 78,960 valid
horizontal samples, 2,964 negative intermediate components and all 40 full
1:1 cases. All 150 legal-finite Area cases match frozen dev6's output bytes.
Independent checks cover both axes, Area, coordinates, normalization, alpha,
padding and invalid intermediate validity. Both in-memory coordinate and
normalization negative controls were rejected at their exact expected oracle;
raw files remain unchanged. Validation SHA256:
`121c92c799570d8656a57d2d0ef56517f4509866886743b1922dc4ac31951dd3`.
The validation records GPU fixture 266b25d separately from CPU validator
7f6644f and hashes every raw plane. Private logs are under
`build/lanczos-minimal-validation/`. GPU execution finished and its processes
exited before the coordinating task reassigned the GPU.

There were zero NR/SR calls and no Neural plane/reuse. Genuine new-kernel model
output requires a fresh actual NR run with controlled history.

## Complete bilingual links from frozen source

The unmodified `build/dev7-budget-final/core/scripts/build.ps1` ran once with
the cached llvm-mingw and ReShade roots. Its embedded source commit remains
266b25d, with the generator's explicit dirty marker. Both Core binaries have
PE numeric version 0.5.0.7 and contain the exact source commit. The first
provenance check incorrectly compared the descriptive FileVersion string
(`0.5.0 dev7 beta`) to a numeric version; its failed record is retained.
The corrected check uses all four PE numeric version fields. No rebuild was
needed. The nrchain shim has no source/version resource and is byte-identical
to the prior shim; it is not claimed to embed the Core source identity.

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| English Core | 1,404,928 | `8c5c7c574971adc9e2d8f0d980a9bb769732b660ecfa9b9c76e0e4448265fa4d` |
| Chinese Core | 1,429,504 | `975554902bdf2280b1868a70cc50c4db16bb415c2dda9b009851b9b2314db836` |
| nrchain | 8,192 | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

The complete log `full-core-build.log` SHA256 is
`236889faa174f5f028f6bf16f96a01a57487a54f2af8e864fc23c8ac6795220c`.
`full-core-build-validation.json` records the source/manifest and actual DLLs.
Warnings remain visible. No payload or private raw file is added to git.
The remote #172 branch was verified at `987ef37df36e36c631e42825524f12b1411bcbc6`
after this link; this independent #84 slice has not been pushed.

Remaining work: live
slot/DRS/Reset/allocation-failure controls, signed-HDR/underflow/overshoot cost
measurement and actual NR/game sequences. This implementation adds a pass,
signed storage and sinc arithmetic; no net speedup or default-policy promotion
is claimed. Other #85/#86 algorithms remain separate until reviewed integration.
