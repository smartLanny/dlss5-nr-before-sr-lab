# dev14 NVFP4 independent research candidate

Baseline: `a51141f32531e7e48b533e749f0f6edaaa17bd1d` (`beta0.5-dev14`).
Branch: `codex/nvfp4-dev14-sm120`. No merge or release is implied.

The owner resumed GPU validation on 2026-09-09 after stopping the competing
Vulkan test. The root process serializes GPU experiments; compilation and CPU
verification can be delegated. Earlier paused-stage evidence remains separate.
The real ReShade Core now completes pre-SR mode2 NR followed by native SR.
The results below are tied to individual local build receipts; they do not
qualify a final package, Auto, game quality or a 10% complete-NR speedup.

## Implemented boundaries

- Only this Core's pre-SR Feature18 is eligible. The game SR, RR, FG, other NR
  owners and post-SR routes retain their native calls.
- The original model DLL is locked to SHA-256
  `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e`.
  Its FFN module must independently match `3fa6f076eecfbb6e19c378f84bbb7025cd805ed4de9a2f9c9b995eb34a7f3d76`.
  The immutable hybrid reference is upstream `ec96f42f8d32a4fc82f7c4558c0ed12a9f90d853`.
- CUDA device selection follows D3D12 LUID. The tested laptop reports SM120,
  82 SMs and driver 610.62. Normal scheduling uses the actual device SM count.
- Preparation, uploads and parameter construction occur outside recording;
  a not-ready Evaluate stays entirely FP8. Precision transitions reset history.
- Resources belong to recording tokens and actual queue/fence submissions.
  Both completion and successful Reset are required for reuse. Unproven
  submissions are quarantined. A possibly recorded error cannot retry FP8 on
  the same list or be converted into a successful NR output.
- Default precision is original FP8. Auto requires an explicit qualified
  artifact policy. No qualified performance/quality policy is shipped yet.
  An explicit developer host experiment is separate from Auto admission.

The source staging entry checks every inherited HLSL block and the NRChain
bridge against dev14. The sole allowed HLSL difference is the exact
`CombineV2` diagnostic gate described below. Both the source block and its
canonical generated form have pinned baseline/candidate hashes and recorded
diffs. Unknown, additional or newly introduced HLSL changes and any NRChain
bridge change are rejected. New CUDA kernels change only the experimental FFN
path.

## Rebuildable kernel work

The new CUTLASS 4.7.1 / CUDA 13.0 SM120 contraction uses dynamic shapes and
source-generated parameters. Four tile/schedule variants exist. Its output
is FP16 `[M,1024]`, with FP32 accumulation and UE4M3 block-16 input scales.
The separate MSVC helper exposes a bounded POD C interface to the MinGW Core.
An artifact-pair fingerprint and hashes bind helper, ABI and cubin.

Recovered pack, polynomial gate, scale layout, original activation layout and
FP8 output epilogue are available as CUDA source with independent CPU oracles.
The epilogue is `FP8(half(gemm) + half(half(skip) * half(cosine)))`, with the
reference's half rounding and saturation; it is not ordinary GELU or a guessed
residual formula. Counter stores depend on an external GPU-wide UAV boundary.

The fused contraction removes the intermediate FP16 output and publishes
directly in the original FP8 layout. Its vector store uses the actual CUTLASS
consumer mapping: adjacent columns belong to lane XOR 4, while half2 arithmetic
pairs two rows in one lane. Both scalar and vector versions passed 66 isolated
GPU cases against independent CPU, recovered epilogue and immutable upstream.
The three-launch pair was also exercised in the owned signed Feature18 host;
eight replacement pairs record 24 launches per ready Evaluate.

`kernels/nvfp4_expansion` implements a true CTA-local input pack followed by
SM120 block-scaled MMA and the integrated gate/FP4 epilogue in one launch.
Together with fused contraction this records two launches and three UAV
dependencies per pair. The signed M640 host confirmed 16 launches/eight pairs
and byte-exact complete NR output versus the three-launch candidate. This
first implementation is substantially slower than the original expansion;
fewer launches alone are not qualification. The padding experiment removes
shared-memory bank conflicts, without changing global layout or arithmetic.
In the isolated `expansion-timings-r3.json` sample, the fastest fused expansion
variant took 0.06932 ms versus 0.01857 ms for reference pack+expansion at M640,
and 0.11392 ms versus 0.03385 ms at M960. These are local kernel measurements,
not complete-NR or game timings; two-launch fusion is not a selected winner.

The integrated expansion epilogue differs from standalone `gate_to_nvfp4`:
there is no positive scale floor, the multiplier uses approximate FTZ reciprocal
with a finite cap, and normalized FP32 converts directly to FP4. The initial
extra half rounding explained all 233 captured mismatches; corrected kernels
matched 66 shape/zero/signed cases and 96 real-weight cases on GPU.
The failed initial outputs remain in `expansion-m640-scope-r1.json` and
`expansion-r1-raw.json`; the corrected evidence is
`expansion-shapes-r3.json` and `expansion-models-r3.json`.

M640 is added coverage for the rebuilt implementation: it is the actual
1920×1080 NR shape (`20×32` internal dimensions), where the upstream complete
hybrid replacement templates are unsupported. M640 isolated math comparisons
and owned-host tests must not be presented as an upstream complete-hybrid
comparison. M960 is the supported common shape used for that comparison and
for the real Core static NR→SR evidence below.

## Real Core static NR→SR evidence

The initial observer timeout is resolved. Installing passive observation
before the Core's first NR runtime load captures the signed FFN identity;
fresh real renderer runs now record eight replacements per ready Evaluate.
CUDA/device preparation still happens later. The native SR call remains in
the chain, and failed observer installation retains original NR behavior.

The accepted static comparison uses NR/input size 2560×1440 (M960), native SR
output size 3840×2160, a 192-frame epoch, 128 warmup frames and 64 timing
samples. Raw input and output were captured at epoch frames 184–191. Pure SR
`S` is the run with Core disabled; `F` is original FP8 NR followed by SR;
the two candidates use upstream reference FFN kernels or our rebuilt plain
contraction. The upstream comparison uses the locked upstream kernels and
weights through the same safe backend, not a test of the entire upstream
plugin. Original FP8 records zero FFN replacements; reference and plain each
record eight pairs, 32 launches and 40 UAV dependencies per ready Evaluate.

An earlier renderer sorted opaque/shadow work using process-dependent
addresses, so even repeated pure SR produced different input bytes. Its r4
results remain diagnostic history and are excluded from same-input quality
evidence. The corrected renderer's two pure-SR runs have matching full-epoch
metadata and byte-identical captured input and SR output in all eight frames.
The corrected `S`, `F`, reference and plain runs also match the full epoch's
metadata and every captured original-input SHA-256. This verifies the captured
frames; it does not claim raw pixels were captured for all 192 frames.

The following are explicit local metric definitions; definitions from an
external research report were not established. For O-grid RGB values,
`rF = F - S`, `rC = C - S`,
`effect_gain = sum(rC*rF) / sum(rF*rF)`, and
`residual_nrmse = sqrt(sum((C-F)^2) / sum(rF*rF))`.
Energies are pooled over all eight frames before normalization, with float64
accumulation, no clipping/gamma conversion and alpha excluded. The FP8
enhancement residual RMS is 0.0034353073; FP8 is a reference, not image-quality
ground truth.

| Candidate | Effect gain | Residual NRMSE | Enhancement RMS / FP8 enhancement RMS | RMS error versus FP8 |
| --- | ---: | ---: | ---: | ---: |
| Locked upstream FFN reference | 0.98630334 | 0.07535483 | 0.98908292 | 0.0002588670 |
| Rebuilt plain contraction | 0.98632293 | 0.07269010 | 0.98890329 | 0.0002497128 |

Reference and plain are not byte-identical to each other: per-frame SR RGB
RMSE is 0.00007220–0.00007776, with maximum absolute differences
0.015625–0.0178223. This static evidence does not qualify motion, disocclusion,
perceptual similarity or game quality. Adjacent-frame error changes in the
reports use fixed image coordinates without motion compensation.

Private evidence IDs under `resume-20260909` are
`chain-fixed-pure-sr-repeat-r1.json`,
`chain-fixed-reference-enhancement-r1.json`,
`chain-fixed-optimized-enhancement-r1.json`,
`chain-fixed-upstream-vs-optimized-r1.json` and
`chain-fixed-enhancement-contactsheet-r1.json`. Raw captures and NVIDIA
payloads are retained privately and are not included in source exports.

## Exact diagnostic-gate candidate

`CombineV2` now computes diagnostic source conversion and final relative delta
only when diagnostics are enabled and both pixel coordinates are multiples
of 32: `(flags & 8) != 0 && (x & 31) == 0 && (y & 31) == 0`.
The two explicit branches move diagnostic-only calculations under the
existing sampling condition. Output arithmetic, enhancement strength,
protections, bindings, resource lifetime and diagnostic sampling are unchanged.

With the actual runtime compiler's O3 `cs_5_0` path, the generated candidate
DXBC is byte-identical to the private GPU-tested candidate. The canonical
generated HLSL hash changes only from
`7cf7fe7ef5eee765387adf2ed28d1c7abeab3a6951388b32d043f1b7544eec1a` to
`eb10b9548d0b8c49d2b048f5c93c4a135004a976ff7162e89cbe611e302adc55`.
The source-stage manifest records this exact contract and the separate source
block contract; the historical inherited-hash field is explicitly identified
as the baseline, while the candidate hash records the actual staged shader.

Against the unchanged reference-kernel profile, all 24 captured input, SR and
display files were byte-identical across eight frames. Both complete color
diagnostic blocks also matched, including their stage statistics and hashes.
Two short order-reversed comparisons saved 0.04029 and 0.04125 ms in composition.
Their complete-NR timings drifted with Feature18, so they do not establish a
complete-NR speedup. Five long three-way paired blocks are now complete: 9,000
valid measured samples under the owner's shared desktop conditions. Relative
to the common-host upstream profile, composition saves 0.040835 ms on average
(whole-block 95% interval 0.039997–0.041846), while complete NR saves 0.050%
with an interval of −2.703% to +2.334%. Whole-NR improvement is not established.
Shared GPU and unlocked CPU power conditions cannot grant formal qualification.
Evidence: `chain-fixed-reference-diag-equivalence-r1.json` (SHA-256
`67f51c503f66646a69ad76d09b4dca682558b8a967f67156276b7ec3e825f381`),
`diagnostic-gate-short-decision-r1.json` and
`diagnostic-gate-source-integration-r1`.

The first 720-frame motion comparison used six versus seven preparation
frames and had small output differences. A same-profile repeat matched
byte-for-byte. A fixed-64 preparation attempt correctly failed before the
measurement epoch because hybrid was not ready. With fixed-512 preparation,
both reference and diagnostic runs had 510 successful preparation NR calls,
then an independent Reset and 128 warmup frames. Across the 64 captured frames
around the second camera reversal, all 320 input/depth/MV/SR/display planes
matched byte-for-byte (`motion512-reference-vs-diag-raw-r1.json`). Preserve the
earlier differences; this experiment does not prove their sole cause or qualify
all motion/disocclusion behavior. See the [cost and experiment report](NVFP4_PERFORMANCE_20260909.zh-CN.md)
for real-Core versus synthetic-input boundaries, common-host upstream scope,
SM82/170 parameter control, and unresolved internal Swin computation/sync costs.
The original-FP8 versus FP8-with-gating motion control also matches all 320
planes. Recomputed fixed-512 enhancement reports for upstream versus gated
upstream agree frame-for-frame: pooled effect_gain=1.00973531 and
residual_nrmse=0.08974568. This retains the existing hybrid-to-FP8 numerical
difference and is not an all-scene quality threshold.

## Evidence obtained before the GPU pause

These results were obtained from working source snapshots, not from a final
frozen release binary. Preserve the exact build receipts alongside each result.

| Check | Actual result |
| --- | --- |
| Signed Feature18 owned D3D12 host, FP8 | Create/Evaluate/fence and finite readback passed |
| Actual 1920×1080 NR shape | `M=640`, internal dimensions `20×32`; unsupported by upstream reference templates |
| Reference hybrid, 2560×1440 | `M=960`, eight actual FFN replacements per ready Evaluate, finite readback |
| Recovered pack/epilogue | Eight row counts including tails, byte-exact GPU and CPU oracle checks passed |
| Recovered polynomial gate | 18 row/tensor-scale combinations, byte-exact GPU/reference/CPU checks passed |
| Rebuilt contraction | 56 variant/shape/input cases passed CPU-oracle, reference-where-available and guard checks |
| Game, final output quality, performance target | Not accepted |

An initial synthetic FP8/reference comparison found nonzero output differences;
finite output and arithmetic equivalence do not establish temporal image quality.
Other games and a Vulkan workload contaminated initial timing. Do not reuse
those short samples as a speedup result or choose a kernel winner from them.

## Reproduction and remaining acceptance

1. `scripts/prepare-nvfp4-assets.py` verifies and extracts pinned reference
   assets to an external private directory. No NVIDIA model is downloaded.
2. `scripts/build-nvfp4-kernels.ps1` builds the helper and kernels;
   `scripts/build-nvfp4-io.ps1` rebuilds the recovered IO reference.
3. `tests/nvfp4_backend/run.ps1`, `scripts/validate-nvfp4-core.ps1` **without
   `-RunWarp`**, and `tests/nvfp4_host/test_performance_analysis.py` are CPU gates.
4. Commit the complete source, then use `scripts/prepare-nvfp4.py` and the
   generated Core's normal build script. The source manifest records the frozen
   commit, all added modules and the exact shader contracts described above.
5. Match each reproduced result to its actual source, helper, cubin and Core
   build receipt. Local fused-kernel and static real-chain checks have run;
   a newly packaged binary does not inherit their qualification automatically.
   Keep M640 added coverage separate from M960 upstream comparisons, and retain
   M2160/M3840 and non-tile-tail checks. No whitelist expansion follows from
   GEMM correctness alone.
6. Use five interleaved 600-frame A/B blocks with fixed settings, clean histories
   and no competing GPU tests. Record complete FFN, Feature18, whole NR chain
   and whole GPU frame separately. The performance analyzer rejects missing
   completion, missing replacements, shared-GPU evidence and GEMM-only NR claims.

The owned host waits for actual replacement readiness, then resets history
and starts a fixed warmup/measurement epoch. Counting warmup from Create caused
different history fixed points when asynchronous preparation completed one frame
earlier; those old measurements are retained but excluded from output acceptance.
Five corrected M640 blocks produced identical optimized/fused output hashes.
The original-FP8 comparison is a numerical difference measurement, not temporal
or game image-quality acceptance. The rebuilt unfused contraction reduced
Feature18 time by about 2.5% in that synthetic experiment; it did not meet the
10% complete-NR target. The first scalar fused variant had no statistically
established Feature18 gain.

`prepare-nvfp4-chain-experiment.py` derives private compiled profiles from a
hashed canonical Core stage without modifying that stage. The canonical
source now includes only the exact allowed diagnostic gate above. The normal
Auto registry stays empty.
The private profile has one-shot readiness/reset exports and collects complete
NR-to-SR timing by exact query ticket; output is buffered until the recording
is fence-complete and reset. Its source changes and kernel bindings have a
separate manifest. Per-process identity distinguishes appended evidence.

Motion captures with depth and vectors have been obtained; they cover a bounded
camera-motion segment and are not full temporal or game qualification. Five
long interleaved blocks on the frozen complete-NR candidate have completed,
with shared-GPU status retained and no established full-NR gain. Game validation
and HDR/DRS coverage remain.
Record the real NVIDIA driver
build identity; a CUDA driver API version is not driver 610.62. The full-NR
10% target, game/temporal quality and Auto policy are still unqualified.

Keep the experiment disabled on startup. The local preview package is for
review and later validation; it is not a verified 10%-faster release.

Primary references: [upstream release](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/releases/tag/v0.7.1-hybrid),
[NVIDIA SM120 example](https://github.com/NVIDIA/cutlass/tree/v4.7.1/examples/79_blackwell_geforce_gemm),
[NVIDIA architecture limits](https://docs.nvidia.com/cuda/blackwell-tuning-guide/index.html).
