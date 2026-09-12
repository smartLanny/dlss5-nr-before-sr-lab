# D16 B / Deferred DLSS experiment — codec implemented, runtime pending

Base: `3c13f69119ddb0a0c1246f0348eb15c8aac64f76` (D16).

## Actual scope
This branch implements a new signed carrier codec, HLSL encode/compose kernels,
exact frame/history contracts, cbuffer layout and executable host tests.
It does NOT create/evaluate the private DLSS SR feature, intercept D16 seams or
provide a functioning GUI/INI toggle. The encode shader is NOT super resolution.
ROG MCP was denied by the conversation platform. No Windows/SM5/WARP/NVIDIA/game
validation or installed binary change. This is an implementation slice, not B ready
for gameplay. No existing default, model, license or active development branch changed.

## Upstream mapping and an important correction
https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/e237f895623742b761f9e5f00067cb3dc62619f4/docs/DEFERRED-NR-DLSS.md
UI: Generate before SR, apply after SR (DLSS); INI: [DlssNr] DeferredDLSS=true.
These are upstream options, not options this commit has exposed in our D16.
Upstream runs ordinary NR/workscale composition to E_R, subtracts X_R, then runs
private SR from R to O. It is not generally a direct Delta_N -> DLSS -> O path.
Keep D16's existing N->R composition initially, with its native R-sized guides.

A existing: mainSR(X_R+edit_R), with real NR at N.
C existing: NR on the clean main-SR image, optionally reduced to Npost.
B target: mainSR(X_R)+decode(privateSR(encode(E_R-X_R))).
NR2 still sees NR1's IMAGE, not a residual. Only the PRIVATE SR sees the carrier.
Nonlinear NR/SR and different input information prevent a general equality to C.

## Implemented alternative: bounded affine carrier plus signed HDR tail
Upstream uses 0.5+0.5*d/(1+abs(d)); its inverse amplifies error near 0/1.
This experiment instead fixes a positive generation-wide range B:

    d=(E_R-X_R)/preExposure
    core=clamp(d,-B,B)
    carrier=0.5+core/(2*B)
    tail=d-core
    reconstructed=(2*clamp(privateSR(carrier),0,1)-1)*B+spatial(tail)

Inverse carrier-error gain is bounded by 2B rather than a pole. FP32 tail retains
signed HDR range; E_R already includes current strength/colour/skin settings, so
none is applied twice. Neutral is 0.5; CPU roundtrip does not prove a dynamic DLSS
history stays neutral. A larger B puts more range through neural SR but increases
absolute carrier-error gain. B must not change per pixel/frame; changes reset history.
B=4 is only an initial experiment, not a calibration for every game's exposure.

Tradeoffs: extra tail texture and spatial reconstruction; large edits in the tail
may blur/alias. Tail is currently bilinear, not depth-aware. These are unresolved
quality/cost tradeoffs, not a claim to outperform upstream. Encode writes FP16
carrier plus FP32 tail/validity. Compose rejects invalid contributing tail samples,
failed SR, wrong frame/exposure and identity mode; clean alpha is preserved.
No scene-linear delta may be applied directly to unrelated sRGB/PQ output.

## Remaining D16 splice to implement B
1. Add an explicit experimental placement, default off. Choosing B in place of
   SR->NR preserves the saved C setting; disabling B restores it. No per-frame
   shared g_config mutation and no global change of all users' processing_start.
2. Extract PrepareBeforeSr's private-image NR/multipass work to obtain owned X_R
   and E_R without replacing the game Color parameter. Reuse NR1/NR2, independent
   histories, N1/N2 and matched residual. Do not also run PrepareAfterSr/FinishAfterSr
   for this source. No second NR owner.
3. Preserve the existing EvaluateScope/source claim across both seams. SameFramePair
   validates pairing, not ownership; it cannot authorize duplicate processing.
4. Encode on R, then call original main SR exactly once on untouched native inputs.
5. Implement a genuinely private NVIDIA Feature1 adapter with separate Create /
   Evaluate params, handle and history. The signed Feature18 helper is not a private
   SR backend. Register/private-scope its handle so our observer never treats the
   private SR as a new game source or unknown native handle.
6. Private SR gets the proven R-sized depth/MV/jitter/reset and unit exposure,
   output O and matching carrier create semantics. Query real runtime caps/sizes;
   no GPU whitelist. Separate Create readiness from first Evaluate with real fences.
7. After successful main SR/private SR and exact pairing, compose private O, use
   existing final HDR/headroom/gamut policy once, then safe copyback. Failure before
   unsafe recording keeps main output; opaque/partial failures use quarantine.
8. Copy only legal active subrects to canonical zero-origin surfaces. Rebuild on
   actual output-only size/format changes too. Use D16 immutable dispatch tables
   and actual all-consumer completion, not round-robin ring-depth assumptions.
9. FG, Present, pause and resize remain distinct tests. Never put yesterday's edit
   on a new source or run NR again on generated frames. Keep RR carry separate.
10. Time mainSR/NR1/NR2/privateSR/encode/tail/compose/copies separately and report
    total source frame time; the NR timer alone hides the added SR cost.

Initial slice: one D3D12 view, same direct queue, canonical scene-linear colour
and known exposure. These are format/semantic capabilities, not game names.
Do not assume archived #175 provider work has already entered this D16.

## Performed validation and Windows continuation
GCC warnings-as-errors and Clang ASan/UBSan: 50,000 signed/HDR roundtrip and
error-injection cases, neutral/finite guards and exposure-domain tests. Shared
frame tests cover 15 mismatch components, one-shot consume, failures, reference
pairing, wrong fence/queue, device removal and poison/unsent work. C++ cbuffer
size=48, private-SR flag offset=32 asserted. HLSL/GPU/real SR have not run.

```
python scripts/validate-d16-edit-experiment.py --mode deferred --cxx <compiler> --shader-compiler <existing-codec-compiler>
```
Uses the actual D16 compile-codec-artifact.exe SM5 CLI. Without a shader compiler
it reports not-run. Next: reflection/WARP pixels, then two DISTINCT real SR handles
on signed/neutral input, then D16 private NR-image injection. Do not expose B as
ready until that vertical path exists.

## Fair comparison and tone mapping
Compare A, B, full C, and reduced C at the SAME NR N. Fix R/O, models, style,
pass count, strengths and scene. Test motion, occlusion, faces, fine geometry,
HDR highlights and cuts; record total source ms/VRAM, not only NR or displayed FPS.
B costs another SR/tail/copies and low-N NR did not see the high-resolution image.
Full post-NR quality plus exactly pre-NR performance is a goal, not an equivalence.

Ordinary B uses the SR-output boundary before the game's final tone map when
that boundary exists. Finished-picture carry is a different late mode:
T(C+delta)-T(C) != delta. Do not transfer an unchanged scene-linear RGB difference
through display encoding. Model-proxy tone mapping is also not the game's final
presentation tone map. A working Present hook does not locate a pre-tonemap hook.

Track under #104. No new universal provider framework, main merge or release.
