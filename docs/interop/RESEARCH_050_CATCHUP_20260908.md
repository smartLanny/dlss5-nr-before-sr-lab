# 0.5 forward catch-up: beta0.5-dev4

This is the #168 research successor to #73 → #115 → #119. The only continuing
research target is `next/0.5`; `codex/050-catchup-20260908` is a temporary PR
branch. It does not merge research algorithms into the stable maintenance line.
The separate colour/composition contract work is #169.

## Exact sources and ancestry

- Maintenance input: `c6389f7ea2b7cf865cc23a3bf0afb2268678e3cd`, PR #166,
  still open against its rehearsal base when checked. Its actual hotfix build is
  `35ef9a826642e0eabcecd46d012167dd52b98105`, beta0.4.6-hotfix.1 / PE 0.4.6.1.
- Complete #119 input: `fefd447033b21cf0ae979aa53b7dddc1a8a3e20c`.
  Its older body SHA `a056d43` is historical.
- Merge-base: `12009d0448fb8bba8c699020382bf5bb6b84dec4`; maintenance/research
  had 203/16 commits exclusive to their respective sides.
- Ordinary merge `1f7378a` has those two input commits as parents. Current
  `main@41696ccb64408397951577481051cbcf6944a446` and
  `next/0.5@7540d38eb864697566585a355301b8b1f720d577` are also ancestors.
- Candidate build source: `bf0ef31d5e822586781a349bfda77bed527a99b6`.
  Later documentation/packaging commits do not change this binary source.

## Conflict disposition

All #119 high-strength/layered implementation, INI persistence, normalization,
history invalidation, recommended settings, coherent picture snapshot, F7
actual-frame flags and regression coverage are retained. Research flags remain
Mode2-only; layers are disabled by default and four coefficients default to 1.
Maintenance defaults of final strength 1.0 and its independent colour policy stay.

The current maintenance RR route/recovery, finite blend/null guard, loader,
retirement, typed views, F8/F7, compact UI and NR percentage control are retained.
`nr_command_state.inl`, `nr_runtime_load_evidence.inl`, `patch-post-compat.py`
and `nr_post_area.hlsl` are byte-identical to the hotfix source input. No duplicate
CanonicalBlendFactor or RuntimeLoadError implementation is added.

The shader keeps the latest edge-weighted, bounded LightingLock and DetailDecision.
The layered branch reuses that weighted neighbourhood only when non-neutral
tone/structure needs it; it does not restore the older unweighted estimator.
Root constants remain 35 for 0.5, including the four layer fields; production
reflection and static size checks cover CPU/HLSL offsets. The descriptor writer
fixture gained the actual typed source SRV field. Maintenance fixture variant 8
is preserved; research fixtures moved to 12–15 to avoid silently replacing it.

The generator has explicit new blob pins and requires both exact parent inputs.
It stages beta0.5-dev4 / PE 0.5.0.4 directly, preserving all source/dirty/ancestry
checks. Historical adapters retain their standalone pins. No 0.4 binary is renamed.
The old dev3 documents remain historical; they are not dev4 validation evidence.

## Validation and reproducibility

The normal `scripts/build-multi-api-candidate.ps1` entry point now runs the
maintenance gates plus 0.5 production math, snapshots, bug regressions and
generated-HLSL WARP positive/negative controls. Use the pinned bridge `28aed409`,
ReShade `4a50d1ed`, DirectXMath `28929ac6` and LLVM-mingw 20260826 UCRT toolchain.
Choose a fresh output directory for every attempt; previous attempts are retained.

Independent neutral comparison uses:

```powershell
./scripts/validate-codec-gpu.ps1 -ToolchainRoot $Toolchain `
  -SourceRoot "$Candidate/source/core" `
  -BaselineSource "$Hotfix/source/core/src/nr_before_sr.cpp"
```

The baseline comes from the actual hotfix source manifest above. With high-strength
protection and layers disabled, 504 real WARP outputs are byte-identical between
the two generated production shaders: both pre/post routes, seven colour flag
combinations, six strengths and six identity/nonfinite/edge fixtures. This uses
nonzero edits, source alpha and nonzero subrects; it does not disable NR globally.
Separately, generated 0.5 WARP passes 71 boost / 227 layer scenarios within 384
codec scenarios, with distinct boost and layer negative controls. Neutral layers
and disabled stored preferences retain baseline bytes.

CPU results include 1044 boost, 155748 layer scalar / 8 normalization, 33 picture
serializer/loader, 408 recommended settings, 156 Windows core TXT/ZIP and 35012
LightingLock-bound assertions. Additional inherited workflow gates were rerun: 38 forwarding, 172 null-raster, 60 exact maintained blend detour checks with the raw-NULL negative control, 74 colour-policy and 70 retirement checks. The old research CanonicalBlendFactor fixture was adapted to the current explicit CommandStateBlendFactorValues detour; production was not changed. Counts are assertions/scenarios, not games.

Source, Windows compile, WARP, hosted CI, NVIDIA Feature18 and actual game results
are separate facts. The complete build passed 46 local gates, including bilingual Core/nrchain
static analysis, compilation/link and the native carrier. Both Core resources
report PE 0.5.0.4. `validation.json` SHA256 is
`c2aac3ae1156ede303e09ea016eb4f48b1baa4e54c6b3a0cc93a3b3c80ef6ccc`.
The neutral-comparison log SHA256 is
`011eda0e96290b98110d8ec318b0d9f668a07a4cfa8df6069adba3aebbe3068c`.
The generated-source manifest and validation file bind every binary below:

- `DLSS5-AI-Rendering-Upscaling-beta0.5-dev4-@野生的装机宅-Bilibili.en.addon64`: `99ffc8328c6efa9ce1625a5303803f874aa8f27a40f13c8dc829d11677d336ff`
- `DLSS5-AI渲染超分版-beta0.5-dev4-@野生的装机宅-Bilibili.addon64`: `838054f39e4390e5851f901d4f9b912980a9964d0f91c55f824014dfe916c3e1`
- `nrchain_nvngx.dll`: `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb`
- `dlss5-native-carrier-045-dx11-compat.addon64`: `092c229b8b4a1746bfc824a7979b84320b663117ffe7adcf339a7d5729597424`

The final package index is the authority for package hashes. Hosted CI has not been claimed passing;
the known billing block must not trigger blind reruns. This candidate has no new
NVIDIA, real-game, performance, HDR or face-mask validation.

## Fixed-scene owner comparison

Exit the game before changing components; back up the working hotfix Core/bridge
and INI. Keep game, model/runtime, scene, camera, R/N/O, exposure, RR/FG and all
non-research controls fixed. Start with SDR, RR/FG off, NR before SR, 100% NR work
and final strength 1.0; confirm active NR and submission counters via F8.

1. Capture the known hotfix reference with F8 and optional F7.
2. Install dev4, disable high-strength protection and layers, repeat that scene.
3. Enable protection only, then compare at 1.0 / 1.2 / 2.0 under identical inputs.
4. Keep the protection choice fixed, enable neutral layers (all 1); then change
   only bright contribution to 0.75. Return all layers to 1 before the next case.
5. Repeat post-SR at 100% and a fixed reduced NR percentage. Research pre-SR
   flags must not imply a new post algorithm. RR must use its automatic safe route.

F7 matrices are sequential captures, not same-frame final-SR pairs. Do not time
performance while capturing. Record the actual F8 version/config generation,
source/model dimensions, counters and visible changes. Roll back by exiting the
game and restoring the backed-up components/INI; no tag or stable release changes.
Human-face #162 remains a maintainer follow-up; this task adds no segmentation.
