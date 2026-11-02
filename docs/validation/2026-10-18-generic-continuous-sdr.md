# #274: optional continuous SDR Generic controls

## Status and scope

This candidate starts from the verified main file snapshot
`024f9cc32778c1056db364d76d087ee257da6502`. Its local branch is
`codex/274-continuous-color-20261018`. The checkout is a shallow source snapshot;
these facts do not establish remote ancestry or supersede other open work.

The implementation, CPU math, configuration/consumer source contracts and
actual UI-branch adapter are reviewable without Windows. The final embedded
codec also passed the Linux DXC/SM6 compilation described below. No Windows
Core or canonical SM5 compilation, WARP execution, NVIDIA/NGX execution,
rendered menu, game capture, frame-time or visual-quality result is claimed.
There is no new release version, game installation, default enablement or
model payload. This is a source review branch, not a separately distributable
Beta 14 build; use the next approved integration identity before packaging.

The change addresses two specific mechanisms confirmed in the current
`GenericHslV1`, `GenericRgbV1` and `ApplyGenericStoredV1` shader code:

1. Restoring relative HSL saturation near an achromatic neural answer can
   restore colour using an undefined, unstable hue.
2. Selecting a bright/dark multiplier from the sign of luminance difference
   and applying it to the whole RGB edit can jump at zero luminance difference
   even when the chroma edit is nonzero.

This does not reimplement model network selection, input curves, reconstruction
filters, guide attachment or multi-layer composition. Those existing contracts
and independent open reviews remain separate.

## Saved settings and menu

`GenericResidualExperiment` remains off by default. The new
`GenericResidualAlgorithm` is persisted with these meanings:

| Value | Menu | Meaning |
| --- | --- | --- |
| `0` or an unrecognized value | Legacy HSL | Existing directional RGB / relative HSL interpretation and consumers |
| `1` | Continuous SDR (experimental) | Separate brightness signal and Cartesian colour candidate, on supported final SDR composition only |

Changing the algorithm keeps the five saved gains. The total range remains
**1–2**; the other four remain **0–2**. The final two labels change from
Saturation/Lightness to Colour/Light-dark only while the new algorithm is
selected. Their new interpretation is explicit; this is not a migration that
silently changes old presets.

The new choice lives in the existing Lighting page, Edits experimental method.
It uses the same single settings transaction. Selecting an algorithm is a
discrete edit; sliders retain the existing release-to-apply behaviour. The
menu differentiates pending, recorded, neutral bypass and unsupported input,
raw reference or low-grid field combinations in English and Chinese.

The existing RGB restoration option defaults to mode `0` (the ratio
compositor). It is **not required** to use this candidate. The ordinary post
ratio path is a supported consumer when its existing input contract is SDR.

## Consumer and colour-domain contract

| Consumer | Legacy HSL | Continuous SDR candidate |
| --- | --- | --- |
| Native `CombineCS` ratio output | Existing behaviour | Allowed only when `CodecColorMode` already equals `LinearSdr` or `SrgbEncodedSdr` |
| Native `CombineCS` RGB option | Existing behaviour | Same SDR admission; no automatic enabling of the RGB experiment |
| Native `CombineV2CS` ratio / independent chroma / RGB | Existing behaviour | Same SDR admission, with raw and low-grid consumers excluded |
| Present SDR `Codec` final `CombineV2CS` ratio / independent chroma | Existing behaviour | Allowed on its existing explicit `Encoding::Srgb` -> linear SDR path |
| `BuildEditFieldCS` / `BuildColorEditFieldCS` | Existing behaviour | Excluded; those fields have no independent brightness/colour-output interface |
| Encode, resample, primary auxiliary preparation | Existing behaviour | No new controls applied |
| Raw model / identity diagnostic reference | Existing raw bypass | Bypasses new controls, before the helper is reached |
| Unknown/model-value-domain/PQ/HDR | Existing behaviour | New controls not applied; visible reason; original legal composition retained |

The current native pre-SR colour policy tags R8 input as model-value-domain,
not as verified SDR. Therefore the newly wired V2 consumer does **not** imply
that the default native pre-SR route is currently admitted. Native post-SR
uses the new controls only when its existing R8 output-encoding contract was
already explicit. Present SDR is a currently reachable supported route. The
menu must not tell users to guess an encoding to unlock the option.

An unsupported continuous request does not silently invoke old HSL. Selecting
Legacy HSL explicitly restores the old interpretation with the saved gains.
SDR admission does not come from the fact that neural values happen to lie in
`[0,1]`, from an unverified-HDR colour permission, or from an FP16 texture.

## Math and final composition

The shared, independently implemented math is in
`src/nr_generic_controls.hlsli`, with the exact marked body embedded in the
production `kCodecShader` in `src/nr_before_sr.cpp`.

The helper consumes the paired proxy and neural answer **after the consumer's
existing decode**, in the same linear SDR coordinates. It returns three
values: a colour candidate, an independent luminance signal and validity.
With `P` the proxy, `N` the answer, `T` total gain and linear Rec.709 `Y`:

```text
candidate = P + T * (N - P)
deltaY = Y(candidate) - Y(P)
controlledY = Y(P) + (max(deltaY, 0) * bright + min(deltaY, 0) * dark) * light
```

The colour gain mixes the proxy and total-scaled candidate in Cartesian
Oklab coordinates. There is no hue-angle branch at the grey axis. This is a
full Oklab-vector candidate interpolation, not a claim that perceptual L stays
fixed: final brightness still comes from `controlledY`. Continuous endpoint
correction cancels matrix round-trip error. Gain 0 selects the exact proxy;
gain 1 selects the exact total-scaled answer without a colour conversion.

All five gains at 1 take the old-result bypass. An identity model answer stays
identity. A colour gain of 0 removes the colour edit without erasing the
separate brightness signal. A light/dark gain of 0 removes that signal without
discarding a nonzero colour edit. The pure helper also tests total 0 as a
mathematical identity endpoint; that endpoint is not a newly exposed UI range.

Both final native compositors consume the separate scalar. Their no-edit
checks account for both signals. The RGB terminal accepts an optional scalar
delta and normalizes its colour candidate by the candidate's own luminance;
the old signature forwards to the exact old interpretation. The V2
independent-chroma branch uses the colour candidate's own luminance. The
default V2 ratio branch retains its existing protected-log formula and colour
budgets, so a slider moving off neutral does not switch its final guard
formula.

The lighting and detail neighbourhoods consume the same independent scalar
formula as the centre. They retain their existing nearest-grid sampling,
two texture loads, luminance guidance and weights; neighbours perform no
Oklab conversion. Both final compositors use one shared lighting-mean helper.
Flag-off and neutral dispatches keep the original raw luminance taps. The
captured detail decision also uses the requested scalar luminance, while the
source/proxy/neural capture planes retain their existing raw-data meanings.

An independent review caught a concrete centre/neighbour mismatch before
delivery: with uniform source/proxy `0.25`, NR `0.5`, bright gain `2`, colour
gain `0`, total/light `1`, and full LightingLock, the centre ratio is `3`.
Uncontrolled neighbours would still use `2`, giving a weighted mean of `2.4`
and an incorrect final grey of `0.3125`. The shared scalar makes all ratios
`3`, so the full lock removes that uniform drift and returns `0.25`. The
regression executes the actual sampling and lighting-mean functions plus
both ratio terminal references and the production RGB terminal. A separate
nonuniform detail fixture must agree with a field whose grey luminance was
controlled before sampling.

Existing confidence, near-black, face, regional, maximum-ratio, gamut and
storage rules remain authoritative. They can attenuate the resulting edit;
separate helper signals do not promise invariant perceptual colour after
those guards. The helper does not add a clamp to signed or over-range finite
candidates, replace an existing codec clamp, or reinterpret HDR. Alpha writes
remain the stored source alpha.

## Work and observation

Legacy Generic keeps RegionFlags bit 5; the new candidate uses unused bit 6.
The existing reconstruction bits are combined afterwards, and the actual
composition constant bank is still captured. There is no new root-constant
layout, model evaluation, pass, texture or readback allocation. Explicit
branch guards keep the new colour conversions behind a nonneutral admitted
request. Instruction/register cost on actual GPUs remains unmeasured.

Algorithm changes participate in `SameConfig`, not the model tuning or
`SameHistoryConfig` identity. They do not create an additional model/history
key. Existing external settings/capture-generation behaviour still applies.

A small CPU receipt records requested algorithm, selected Generic flag,
reason, config generation, time and native/Present producer. A receipt is
current only while enabled, for the same config generation and within two
seconds. The UI also compares its displayed generation. Feedback and observed
capture metadata name the requested algorithm. These are recorded constants
and settings, **not GPU completion or proof of visible colour**.

## Verification completed on this source

Run from the repository root with an available C++20 compiler:

```bash
python3 tests/test_generic_continuity.py
python3 tests/test_lighting_edge_weights.py
python3 tests/run_colour_restore_math.py
python3 tests/test_reconstruction_runtime.py
python3 scripts/validate-reconstruction-math.py --compiler /usr/bin/g++ --output /tmp/nr-274-reconstruction-20261018
CXX=/usr/bin/g++ python3 tests/test_picture_reset.py
CXX=/usr/bin/g++ python3 tests/test_nr_dual_observation.py
python3 tests/test_unified_ui.py
python3 tests/test_compat_capture_ui.py
git diff --check
```

The new runner requires its control and restore HLSL markers to match the
real shader exactly. It compiles those bodies through a minimal host-vector
adapter (`-ffp-contract=off`) and checks the actual RGB terminal plus expressly
limited post/V2 ratio references. The actual nearest-grid lighting/detail
sampling functions and detail filter also run through texture adapters, with
their production control flow and arithmetic retained. It also compiles the actual new UI branch
and receipt implementation through widget/clock seams. This is not HLSL or
Win32 compilation.

The new UI/receipt adapter passes **66 checks** in both languages, including
saved values, discrete/slider binding, pending/stale/generation rejection and
visible unsupported reasons. The new math run passes **64,120 assertions**, including endpoint identity,
independent target Y, colour-only and brightness-only final output, invalid
samples, uniform LightingLock, nonuniform detail consistency, centre/tap gain
equivalence, flag-off exact sampling, signed/over-range near-neutral candidates and deterministic small
edits. In the two local sweep reproducers, the maximum adjacent final-RGB
step is `1.20699406e-6` at the grey axis and `1.43051147e-6` at the bright/dark
zero crossing. These numbers describe those fixtures, not a game-quality
score or a global continuity bound.

Six deliberately broken variants must first compile successfully and then
exit with the expected assertion failure:

| Mutation | Required final-output failure |
| --- | --- |
| Reinstate sign-selected whole RGB scaling | Bright/dark zero-crossing discontinuity |
| Use the actual production legacy HSL colour function | Grey-axis discontinuity |
| Ignore the new scalar in the production RGB terminal | Colour 0 incorrectly erases brightness |
| Reinstate a brightness-only no-edit gate | Light 0 incorrectly erases a nonzero colour edit |
| Restore raw NR luminance in lighting taps | Full LightingLock leaves uniform controlled drift |
| Restore raw NR luminance in detail taps | Stabilized detail differs from the controlled model field |

Existing shared colour restoration passes 200,080 checks. Reconstruction
math passes 115,423 checks and its source/runtime suite passes 8 tests. The
picture-reset fixture passes 48 checks, compatibility F7 controls 56 checks,
and NR2 observation/face/runtime/serialization fixtures pass 17/7/18/7 checks.
Unified UI source checks pass with its generated-export-only case skipped.
The Windows-only D13 UI and settings-notices harnesses were skipped because
the pinned Windows compiler/runtime is unavailable; a skipped test is not a
compile result. A first picture-reset invocation lacked its default `clang++`;
the listed `CXX=/usr/bin/g++` invocation completed successfully.

The unchanged legacy lighting estimator passes 7,324 analytic edge/weight
checks, including the original four paired loads and zero loads when disabled.
The selected lighting-attenuation case from `validate-bug-sweep.py` passes
35,012 checks using the actual final shader block. The post-light script's
production CPU oracle passes 12,015 assertions and all four of its deliberately
broken variants fail as expected. Its subsequent historical source-comparison
stage cannot run in this shallow snapshot because `c237ac9` is unavailable;
the full script is therefore not reported as passing. The three existing
adapters now extract the shared mean helper without changing their oracles.

## Final production HLSL compilation on the CPU host

After the lighting/detail-neighbour correction, the exact `kCodecShader` was
extracted without shader edits and compiled with Microsoft's official Linux
[DXC v1.9.2609 release](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609).
The inventory matches `prepare-codec-artifacts.py`: all 15 compute entries,
the two face variants, eight reduced-domain variants and four combined
face/reduced variants. All **29 variants passed**, with zero compiler failures.

```sh
python3 scripts/validate-codec-dxc.py \
  --source src/nr_before_sr.cpp \
  --dxc /path/to/dxc \
  --output /path/to/new-empty-receipt-directory --workers 3
```

The script downloads nothing and refuses an existing output directory. It
records the compiler version, source/shader/compiler hashes and each DXIL
artifact's hash. The committed [DXC receipt](NR_CONTINUOUS_SDR_DXC_20261018.json)
contains no shader binaries, vendor payload or machine path.

| Item | Recorded identity |
| --- | --- |
| Final `src/nr_before_sr.cpp` SHA256 | `1fe3b389a77ca3d87dc65dda8687e20243227e26b78b5c58f26cf3526db138fb` |
| Extracted HLSL SHA256 | `fdd05dd3de3e4059368181845437457d9643fe831ab4f26f88fdf30bf14a7b96` |
| Compiler version | `libdxcompiler.so: 1.9(5512-01b62ad4)(1.9.2609.5)` |
| Official release archive SHA256 | `96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1` |
| Committed validation JSON SHA256 | `bcfb24d29fe963b5630a480bc87e0c2fde0afd09f4b35e5adad7b1e453bf2000` |

This uses `cs_6_0`, HLSL 2018 and optimization level 3. It checks the actual
shader's compiler acceptance; it does not execute the shader or replace the
product's Windows `D3DCompile`/`cs_5_0` gate. The generated DXIL files must not
be placed in the Core's SM5 shader cache or distribution package.

## Remaining Windows and game work

Use the existing canonical clean-checkout build and shader validation with
the repository's pinned Windows inputs. Verify both default/legacy and the
new option, keeping build identity distinct from visual acceptance. Then use
the existing capture path for one supported SDR scene that exposes the #274
grey/direction problem. Keep network, passes, precision, source extent and
settings fixed while comparing Legacy and Continuous. Confirm colour 0,
light 0, all-1 and an unsupported HDR request in the recorded metadata.

Actual shader readback, scaled/odd/subrect dispatch, 61/99/100 precision,
multi-layer composition, game UI alpha, runtime stability and frame times
remain untested here. Existing guards and the optional nature of this change
do not establish those results. No new paid CI or additional benchmark
platform is required by this candidate.

## References checked

- [#274 grey-axis and directional analysis](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/274#issuecomment-5973254204).
- [#274 C0/C1/C2 integration specification](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/274#issuecomment-6054456195).
- [#274 October 17 model follow-up](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/274#issuecomment-6233033039) and [#309 model/network contract](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/309#issuecomment-6233027890).
- [Magpie latest inspected colour shader](https://github.com/SAOG0721/Magpie/blob/d687997780ac8d6e54d8b21a0c6b2be70f5eb401/src/Magpie.Core/DLSSNRColorShader.h) and [detail shader](https://github.com/SAOG0721/Magpie/blob/d687997780ac8d6e54d8b21a0c6b2be70f5eb401/src/Magpie.Core/DLSSNRDetailShader.h). At inspection those shader bytes match the October 8 reference `2fceab5e241bc9f8ded001ab3266762f1f8bc51e`; a newer 0.6.10 label alone was not treated as a newer colour algorithm.
- [Björn Ottosson's Oklab definition and public-domain matrices](https://bottosson.github.io/posts/oklab/), 2021-01-25 matrix revision. The math is independently implemented from that definition; no Magpie application code or gamut/protection constants are included.
