# One-launch pack and expansion

This experiment performs original-layout FP8 packing, block-scaled NVFP4 GEMM,
the original half polynomial gate, and output FP4/UE4M3 scale publication in one
CUDA kernel. Pairing it with the separately validated fused contraction produces
two FFN launches. It has no internal CUDA launch, global packed-input tensor,
global queue, cooperative-grid barrier, or inter-CTA producer/consumer protocol.

The kernel is not connected to renderer Auto by this directory. Exact model and
module identities, complete-scope admission, prepared-resource leases, history
transitions and GPU completion remain the backend owner's responsibility.

## Architecture

Each 256-thread CTA packs a private token tile into shared memory. Its eight
warps calculate 128 expansion features; tokens are distributed over a compile-
time 32-, 64-, or 128-token tile. The SM120
`mma.sync...m16n8k64...e2m1.e2m1...ue4m3` instruction consumes packed register
operands and four block-16 scales. The CTA's private packed input feeds these
registers directly. Weights retain their locked global FP4/scale representation.

The register and scale mappings are checked against NVIDIA CUTLASS 4.7.1's
`MMA_Traits<SM120_16x8x64_TN_VS>`. Offline disassembly confirms
`OMMA.SF.16864.F32.E2M1.E2M1.UE4M3.4X` in all three compiled entry points.

| Variant | Token tile | Feature tile | Dynamic shared memory | r2 registers | Spills |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 64 | 128 | 36,864 bytes | 57 | 0 |
| 1 | 32 | 128 | 18,432 bytes | 40 | 0 |
| 2 | 128 | 128 | 73,728 bytes | 94 | 0 |

Different feature CTAs repeat the input quantization. This removes launch and
global intermediate costs, but introduces redundant packing work. Real GPU
measurements must determine whether any variant improves the complete FFN/NR
chain; launch count alone is not a speedup result.

## Exact arithmetic boundary

Input packing matches `pack_original_fp8`: decode two E4M3 values, obtain the
maximum over 16 values, encode `max(amax/6,1/512)` to E4M3, normalize, round to
half, then convert to packed E2M1. Zero or invalid input rows never cause an
out-of-bounds original-layout read.

The output is the **integrated** `expansion/fused_expand.cubin` epilogue. It
rounds GEMM to half and evaluates the recovered half polynomial, but its final
block-scale conversion differs from the separate `gate_to_nvfp4` IO helper:

1. Encode `amax * (norm/6)` to E4M3 without a positive minimum scale.
2. Decode the scale and apply `rcp.approx.ftz.f32`.
3. Multiply by `norm` and cap the multiplier at `FLT_MAX`, propagating NaNs.
4. Normalize in FP32 and convert directly to FP4, with no extra half rounding.

The current API explicitly accepts only `inverse_tensor_scale == 1.0f`.
The polynomial's own half multiply/FMA rounding remains unchanged.

The first source snapshot incorrectly reused the standalone output conversion.
The GPU owner found 146 signed-input and 87 real block-31 FP4 differences at
M640, identical across all tile variants, plus a zero-scale difference. CPU
analysis of the saved operands predicted every differing nibble: for example,
`37.5 * float32(1/30) == 1.250000119...` rounds directly to FP4 1.5, while an
extra half rounding produces the exact midpoint 1.25 and rounds to FP4 1.0.
The original zero scale was byte `00`; the first snapshot wrote `01`.

r2 implements the actual integrated epilogue. The GPU owner reported byte-exact
FP4/scale results and intact guards for the initial M640 zero, signed and real
block-31 cases. Wider tensor checks, backend NVAPI integration, temporal imagery
and complete-chain performance are separate evidence; this directory's build
and CPU tests do not claim those results.

## C ABI and recording contract

`expansion_abi.h` defines Input/Params/Info of 128/56/184 bytes. The helper is
plain C++ and imports no CUDA driver. It verifies dimensions, declared actual
SM120 capability, shared-memory capacity, alignment, tensor scale, each buffer
extent, address overflow and output overlap before emitting Params. It does
not establish adapter identity; the caller supplies fields from its matched
D3D12/CUDA device.

For actual token count `M`, let `P=ceil(M/128)*128`:

- Original FP8 access extent: `ceil(M/16)*16*1024` bytes. Only logical rows below
  M are read; the 16-row physical permutation explains this bound.
- Packed weights: 2,097,152 bytes. Weight scales: 262,144 bytes.
- Packed output: `P*2048` bytes. Output scales: `P*256` bytes.
- All five GPU buffer addresses require 16-byte alignment. Output ranges must
  not overlap each other or any input range. Read-only input ranges may overlap.
- Grid: `[32,P/token_tile,1]`; block: `[256,1,1]`. Use returned geometry and
  shared-memory size rather than duplicating this calculation in a recorder.

All Params can be built before recording. The only runtime patch is the
explicit `linear_input_offset`/`linear_input_bytes` region reported by the
helper: currently byte offset 0, length 8, containing the original FP8 address.
It comes from the matching original expansion's `p[0]`. The complete Params
blob is private to its recording lease. A legal private read-only placeholder
can be used during preparation and must be replaced before launch.

The owner records an initial UAV dependency, this expansion, another UAV
dependency, the matching fused contraction, and the publication UAV dependency.
There is no original FFN fallback after a replacement may have recorded.
Neither a launchable shape nor this helper's declared limit enlarges renderer
admission without the required reference and end-to-end evidence.

## Reproduction

`scripts/build-nvfp4-expansion.ps1 -DependencyRoot <deps> -RunCpuTests` compiles
the SM120 cubin and the independent helper, then runs CPU-only checks. Use a new
`-OutputDirectory` for a revised candidate while the GPU owner tests an earlier
one. The receipt binds source hashes, CUDA, CUTLASS, MSVC, options and both output
artifacts to a build ID. r1 and r2 private outputs are retained separately.

The CPU suite checks 92,063,936 CUTLASS fragment, scale, physical-read, unique-
writer and tail conditions over all variants, plus 555 ABI/negative cases.
No CUDA context is initialized by these tests.

Only the GPU owner should run `tests/nvfp4_expansion/reference_gpu.py` with its
explicit `--allow-gpu` switch. It verifies pinned upstream assets, then compares
the candidate against **both original kernels**, `pack_original_fp8` followed
by `fused_expand`, including full FP4/scales and guards. Defaults include actual
M640/M960 and non-tile tails. `--blocks` can cover all eight real weight slices.
`--capture-dir` saves private operands, original packed intermediates, both
outputs, Params, hashes and per-coordinate differences. `diagnose_cpu.py` can
analyze those saved files without loading CUDA or re-running a GPU workload.

Primary implementation references are the pinned CUTLASS 4.7.1
`cute/atom/mma_traits_sm120.hpp`, `cute/arch/mma_sm120.hpp`,
`cutlass/epilogue/fusion/sm120_visitor_store_tma_warpspecialized.hpp`, and the
immutable upstream `ec96f42f8d32a4fc82f7c4558c0ed12a9f90d853` expansion SASS.
