# OpenNR performance investigation (source-only, 2026-09-23)

Reference: [clshortfuse/openNR](https://github.com/clshortfuse/openNR),
`62587ae0f581be8e8bc3bb01619f6e3b8efd1983`. It was actually cloned and the
portable source tests executed on the owner's Mac. The Windows connector
returned an account-connection HTTP 400 before opening a workspace. No NVIDIA
host command, vendor DLL extraction/build, CUDA assembly, model evaluation or
performance measurement took place in this session.

This directory is NOT in the Core's production input manifest. No OpenNR code,
NVIDIA SDK, weights, PTX, cubin or runtime DLL is vendored into the plugin.

## What the inspected source establishes

The pinned README/profile accepts only input SHA256
`E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`
and size 165840496. Golden output is
`E67DEE209320CDAFE0E93E45675D7AA34323A53ACC57A72B2E40A181581C989A`.
A reproducible output hash is not runtime compatibility or a speed result.
The user's currently installed 40-series DLL was NOT accessed in this session;
do not assume it differs from, or equals, this reference output.

`src/main.cpp` builds compatibility images while retaining original SM120
images. `src/architectures.hpp::LowerOriginalPtxForAda` independently targets
SM89 and preserves FP8 MMA. It lowers a supported bulk copy into 16-byte
per-lane cp.async operations, in 512-byte warp portions, then emits commit and
wait_group 0. Synthetic 512/1024/2048/8192-byte inputs produced 1/2/4/16 copy
instructions per lane; this is source transformation, NOT an instruction-time
or frame-time ratio. Unsupported 513-byte input was rejected.

The test runner compiles the actual upstream text/architecture/transform
headers and the author's original portable test sections. It excludes Windows
PE, BCrypt, process-spawn and fatbin binary tests, which are not passed by this
run. 67 assertions passed with Apple Clang C++20 + ASan/UBSan; the reference
checkout remained clean. This neither assembles PTX nor validates transformed
instructions on a GPU.

```sh
python experiments/opennr/validate_source.py --opennr-root <clean-reference> --compiler <c++20-compiler> --output <new-evidence> --sanitize
```

## Separate the optimization targets

**40-series compatibility kernels:** profile the copy expansion, synchronization,
register caps, spills and shared-memory occupancy on the actual selected SM89
kernels. Changing ca/cg, grouping copies or moving waits is NOT safe merely
because a static instruction count shrinks. Preserve lane participation,
barrier visibility and publication contracts; test differential values before
accepting throughput changes. If the existing compatible DLL has the golden
hash, reconstructing it is not a new acceleration.

**50-series original SM120:** the reference OpenNR recipe does not optimize the
retained original kernels. Its immediate value is repeatable extraction,
launch/module attribution and standalone profiling. Identify the actual hot
modules first; do not claim the compatibility build accelerates an unchanged
SM120 kernel. Use the GPU-appropriate runtime, not an uncontrolled two-DLL A/B.

**NVFP4:** reuse coordinator #211 and historical #212 source
`57c4680d6ac829c272da616772c378a5b07213e3` (closed, unmerged at lookup), not a new
parallel FP4 plan. Existing contracts already require exact Feature18 identity,
LUID/actual SM count, dynamic token M, full expansion+contraction+packing+
epilogue cost, and no original replay after partial replacement. Keep sensitive
history/reduction behavior and motion-quality validation separate from a GEMM
benchmark. No production NVFP4 hook or weights are introduced here.

## Concrete Windows continuation (isolated; do not touch games)

1. Inventory GPU/driver/LUID and hash the actual intended input DLL before
   loading anything. Record true model working size, layer count and source
   sequence. Keep input/capture hashes with the run; do not export vendor data.
2. Follow the pinned profile's CUDA/tool versions and OpenNR `verify-base`,
   `inspect`, then explicit build/test commands in its README. Keep generated
   output outside game directories. A reference-compatible build must meet its
   golden hash, not have guards removed to admit new code.
3. Run native reference and compatibility differential tests for the SAME GPU
   and supported instructions. Existing explicit entry points are:
   `python benchmarks/run.py cuda-build fp8-mma --sm <actual-SM>` and then
   `cuda-run`; these are isolated arithmetic checks, NOT E2E inference timings.
4. Use `benchmarks/native/` only with its pinned local SDK dependencies. Inspect
   its launch recorder and D3D12 profile formats; do not copy private-offset
   hooks into the general Core without version/ownership gates.
5. Feed `dlssnr-kernels.csv` and `dlssnr-trace.json` to
   `python benchmarks/run.py profile-report <capture> <new-report>`.
   `benchmarks/analysis/profile_report.py` excludes evaluation-001 from hot
   ranking, requires matching launch/timestamp/event counts and rejects
   overlapping intervals. Preserve those guards. Kernel totals are not full
   application frame time: remeasure uninstrumented NR and same-workload game
   real-frame p50/p95/p99 separately. D3D12 Evaluate CPU duration is not GPU time.

Match this data to our existing raw-input/output and model-boundary experiments,
not a new unrelated benchmark image. Cold Create, warm Evaluate, switching,
NR-only and NR+SR/RR+FG require separate receipts. No speed percentage is claimed
until the NVIDIA measurements exist. Official profiling context:
[GPU Trace](https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-overview.html)
and [timeline/interop](https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-ui.html).
