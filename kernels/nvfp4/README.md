# Rebuildable SM120 contraction

This component contracts block-16 UE4M3-scaled E2M1 activations `[M,4096]`
with similarly scaled weights `[1024,4096]`, accumulating in FP32. Four
contraction variants write FP16 `[M,1024]`. An additional experimental fused
variant rounds the accumulator to FP16, applies the recovered residual
epilogue, writes the original FP8 permutation, and updates the three protocol
counter arrays. It does not quantize a model or fuse the input pack/expansion
mainloop. Existing reference assets remain available for complete-path checks.

`contract_kernel.cuh` instantiates the SM120 CUTLASS collective builders used
by NVIDIA example 79a. The shape passed to CUTLASS is `[1024,M,4096,1]`; its
column-major output is the row-major activation buffer expected by the old
epilogue. K=4096 and N=1024 remain fixed. Mathematical tails in M are supported
by the kernel, but this does not widen the admitted shapes of the full NR path.

Build with `scripts/build-nvfp4-kernels.ps1 -DependencyRoot <external-path>`.
`-Bootstrap` fetches only pinned CUDA 13.0 build/tool components and CUTLASS
4.7.1, verifies NVIDIA archive hashes, and unpacks outside the source tree.
No driver installation occurs. CUDA 13.0's actual `host_config.h` accepts
MSVC 19.44; this implementation uses VS2022 17.14's compiler without
`--allow-unsupported-compiler` or CUTLASS source patches. Native compilation
requires the standard relaxed-constexpr/extended-lambda flags. Unused-variable
and defaulted-annotation warnings in vendor templates are suppressed. `/MT`
keeps ownership private to the helper and matches static CUDA runtime linkage.

The builder returns a versioned C POD description and an opaque, dynamically
sized Params blob; the Core never patches a CUTLASS member offset. A trusted
receipt binds the helper, cubin, source/toolchain fingerprint, and kernel ID.
The DLL's `nr_nvfp4_build_id_v1` must agree with that receipt. A Params size
match alone never establishes compatibility. The builder queries the CUDA
device whose LUID matches the D3D12 adapter, verifies SM count and shared-memory
limits, and encodes descriptors while that device's primary context is current.
Calls belong in preparation, before recording any renderer GPU work.

`nvfp4_fused_abi.h` is a separate ABI extension: 144-byte input, 216-byte info.
The existing contraction-only input/info remain 96/200 bytes. Six changing
original-model pointers are represented by a public 48-byte runtime-bindings
POD. The builder computes its offset from the actual compiled kernel type.
After cold descriptor preparation, recording may only copy this POD into a
private preallocated Params buffer. TMA descriptors must never be patched.
The observed fused layout has 2176-byte Params aligned to 128, a runtime region
at byte 1280, and 83968 bytes of dynamic shared memory; callers must query the
compiled helper rather than hardcoding those observed values.

The fused visitor disables the standard CUTLASS D store (`ElementD=void`), so
there is no FP16 intermediate write/read. It retains the reference's two half
roundings: `half(acc)` plus `half(skip * cosine)`, followed by an FP16 add and
saturating E4M3 conversion. Counter stores are deliberately ordinary stores.
The caller must insert a global UAV dependency immediately after the fused
kernel and before admitting another consumer. A launch failure may already
have recorded writes and is never permission to retry the original FFN.

The build receipt includes the exact source hashes of both
`kernels/nvfp4_io/original_layout.cuh` and `original_io.cu`, as well as the
CUDA/CUTLASS/MSVC versions and all kernel/helper source. Cached cubins are only
reused when their source/toolchain inputs and recorded SHA256 still agree.

`tests/nvfp4_kernel/benchmark.py` uses private CUDA-driver allocations, an
independent CPU FP4/scale-layout oracle, output guards, and the pinned upstream
contraction cubin where its supplied templates exist. Its timings describe
isolated contractions, not an NR pass or game FPS. `abi_checks.py` exercises
the actual DLL's version, shape, adapter, capacity and alignment rejection and
does initialize a CUDA context. `abi_cpu.py` is the separate CPU-only gate:
it does not initialize CUDA and only calls early rejection/metadata exports.

Validation as of 2026-09-09:

- Before the owner's GPU-test pause, 56 contraction cases passed: four
  variants, seven M values (640, 960, 2160, 3840, 641, 961, 2173), zero/signed
  operands, independently swizzled nonuniform scales, exact FP16 CPU results,
  output guards, and exact original-kernel results where templates exist.
- Thirteen actual-DLL rejection checks also passed before the pause. Those
  evidence JSON files preserve their original artifact hashes/build identity.
- The fused cubin and its helper compile; CPU-only ABI, 48-byte region guards,
  eleven packed-prefix permutations, and artifact receipt checks pass.
- **Full Core NR/SR, temporal/game quality and the complete-NR performance
  target remain unqualified.** The resumed kernel and signed-host results below
  do not admit production Auto. External games contaminated the earlier short
  timings; those samples are not used to choose a winner.

After renewed owner authorization, both scalar and vector fused contractions
passed 66 GPU comparisons, including runtime rebinding, packed tails, guards,
counter values and a subsequent ordered CUDA consumer. The real signed M960
host produced identical complete NR output for the three/four-launch paths.
This still does not qualify renderer Auto or temporal game quality.

The vector implementation uses half2 across two rows and exchanges FP8 results
with lane XOR 4 before aligned 16-bit output stores. A CPU program enumerates
the actual CUTLASS layout; changing that layout requires revalidation.
Pack-plus-expansion is implemented separately in `nvfp4_expansion`; its first
performance result is a regression despite correct two-launch output.

Primary references:

- https://github.com/NVIDIA/cutlass/blob/cb4247394dd82148787aed73e5dc7cef33cbf862/examples/79_blackwell_geforce_gemm/79a_blackwell_geforce_nvfp4_bf16_gemm.cu
- https://github.com/NVIDIA/cutlass/blob/cb4247394dd82148787aed73e5dc7cef33cbf862/include/cutlass/detail/sm100_blockscaled_layout.hpp
- https://docs.nvidia.com/cuda/archive/13.0.0/cuda-installation-guide-microsoft-windows/index.html

CUTLASS is copyright NVIDIA Corporation and Affiliates, BSD-3-Clause.
Its source and license are retained in the separate pinned dependency tree.
