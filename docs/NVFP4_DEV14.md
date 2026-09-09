# dev14 NVFP4 independent research candidate

Baseline: `a51141f32531e7e48b533e749f0f6edaaa17bd1d` (`beta0.5-dev14`).
Branch: `codex/nvfp4-dev14-sm120`. No merge or release is implied.

The owner resumed GPU validation on 2026-09-09 after stopping the competing
Vulkan test. The root process serializes GPU experiments; compilation and CPU
verification can be delegated. Earlier paused-stage evidence remains separate.

## Implemented boundaries

- Only this Core's pre-SR Feature18 is eligible. The game SR, RR, FG, other NR
  owners and post-SR routes retain their native calls.
- The original model DLL is locked to SHA-256
  `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e`.
  Its FFN module must independently match `3fa6f076eecfbb6e19c378f84bbb7025cd805ed4de9a2f9c9b995eb34a7f3d76`.
  The immutable hybrid reference is upstream `ec96f42f8d32a4fc82f7c4558c0ed12a9f90d853`.
- CUDA device selection follows D3D12 LUID. The tested laptop reports SM120,
  82 SMs and driver 610.62. No `170`-SM scheduling assumption remains.
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
bridge against dev14. New CUDA kernels change only the experimental FFN path.

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

`kernels/nvfp4_expansion` implements a true CTA-local input pack followed by
SM120 block-scaled MMA and the integrated gate/FP4 epilogue in one launch.
Together with fused contraction this records two launches and three UAV
dependencies per pair. The signed M640 host confirmed 16 launches/eight pairs
and byte-exact complete NR output versus the three-launch candidate. This
first implementation is substantially slower than the original expansion;
fewer launches alone are not qualification. The padding experiment removes
shared-memory bank conflicts, without changing global layout or arithmetic.

The integrated expansion epilogue differs from standalone `gate_to_nvfp4`:
there is no positive scale floor, the multiplier uses approximate FTZ reciprocal
with a finite cap, and normalized FP32 converts directly to FP4. The initial
extra half rounding explained all 233 captured mismatches; corrected kernels
matched 66 shape/zero/signed cases and 96 real-weight cases on GPU.

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
   commit, all added modules and inherited shader equality.
5. After graphics testing resumes, validate fused outputs/publication first,
   then the owned NR host, then dev14's real NR→SR path and games. Test the actual
   M640 path as well as M960/2160/3840 and non-tile tails. No whitelist expansion
   follows from GEMM correctness alone.
6. Use five interleaved 600-frame A/B blocks with fixed settings, clean histories
   and no competing GPU tests. Record complete FFN, Feature18, whole NR chain
   and whole GPU frame separately. The performance analyzer rejects missing
   completion, missing replacements, shared-GPU evidence and GEMM-only NR claims.

The owned host now waits for actual replacement readiness, then resets history
and starts a fixed warmup/measurement epoch. Counting warmup from Create caused
different history fixed points when asynchronous preparation completed one frame
earlier; those old measurements are retained but excluded from output acceptance.
Five corrected M640 blocks produced identical optimized/fused output hashes.
The original-FP8 comparison is a numerical difference measurement, not temporal
or game image-quality acceptance. The rebuilt unfused contraction reduced
Feature18 time by about 2.5% in that synthetic experiment; it did not meet the
10% complete-NR target. The first scalar fused variant had no statistically
established Feature18 gain.

`prepare-nvfp4-chain-experiment.py` derives private compiled profiles from an
unchanged, hashed canonical Core stage. The normal Auto registry stays empty.
The private profile has one-shot readiness/reset exports and collects complete
NR-to-SR timing by exact query ticket; output is buffered until the recording
is fence-complete and reset. Its source changes and kernel bindings have a
separate manifest. Per-process identity distinguishes appended evidence.

Initial real-ReShade integration reached mode2 NR and native SR, but timed out
waiting for observed FFN identity. Core loaded/initialized the signed runtime
before installing its NVAPI query observer. The corrected cold `ArmObserver`
installs only passive observation before the Core's first NR load, including
when precision begins Off; CUDA/device preparation remains later. Original NR
continues if observer installation fails. CPU cold-install/failure/concurrency
tests pass; a fresh real renderer run must confirm the cache-order diagnosis.

Keep the experiment disabled on startup. The local preview package is for
review and later validation; it is not a verified 10%-faster release.

Primary references: [upstream release](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/releases/tag/v0.7.1-hybrid),
[NVIDIA SM120 example](https://github.com/NVIDIA/cutlass/tree/v4.7.1/examples/79_blackwell_geforce_gemm),
[NVIDIA architecture limits](https://docs.nvidia.com/cuda/blackwell-tuning-guide/index.html).
