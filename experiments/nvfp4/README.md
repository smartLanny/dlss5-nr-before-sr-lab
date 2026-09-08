# NVFP4 FFN optimization lab — host groundwork, not a live accelerator

Source baseline: b68a071373d902f2f1cd9fe2090fc961bb0706e8. Date: 2026-09-09.
Production NGX paths, settings, RR policy and runtime binaries are unchanged.
No weights, cubins, vendor headers, parameter blobs or proprietary DLLs are included.

## Pinned review

Upstream v0.7.1-hybrid resolves to ec96f42f8d32a4fc82f7c4558c0ed12a9f90d853:
- https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/ec96f42f8d32a4fc82f7c4558c0ed12a9f90d853/OptiScaler/dlssnr/DlssNrNative.cpp
- https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/ec96f42f8d32a4fc82f7c4558c0ed12a9f90d853/OptiScaler/dlssnr/DlssNrHybridBuilder.h
- https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/ec96f42f8d32a4fc82f7c4558c0ed12a9f90d853/OptiScaler/dlssnr/DlssNrHybridAssets.h
- https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/releases/tag/v0.7.1-hybrid
- https://developer.nvidia.com/blog/introducing-nvfp4-for-efficient-and-accurate-low-precision-inference/
- https://github.com/NVIDIA/cutlass/blob/main/examples/79_blackwell_geforce_gemm/79a_blackwell_geforce_nvfp4_bf16_gemm.cu

The release ZIP was not compared byte-for-byte to this source. Do not mix a moving branch, copied metadata or another package with the tag. The current project main source was read separately; its previous NR/SR and RR history is not a safety proof for CUDA interception.

## Source findings

- ContractPaths accepts internal M=960,2160,3840, derived from two fields at offset64 in a 72-byte original FFN argument block. These are NOT screen pixel counts. Launch accepts count==1; count!=1 passes through unless a pair is pending.
- The matcher targets cc_vit_1d_ffn_expand_publish_fp8, cc_vit_1d_ffn_expand_chained_fp8 and cc_vit_1d_ffn_contract_chained_fp8, fixed original module hash/size, launch geometry and pointer aliases. It advances blocks31..38. Both halves are replaced for accepted pairs; other operators remain original.
- Each accepted pair still has input packing, fused expansion, contraction, a separate output/skip/cosine/completion epilogue, explicit UAV barriers and contraction workspace reset. Fixed-bit-width savings are not full FFN savings.
- Vendor() copies a parameter blob and builds a string-keyed map on every invocation. Resolve manifests once, bind per fenced workset and cache immutable descriptors. This is a CPU optimization, not a demonstrated GPU win.
- FusedInput hardcodes SM count170; the builder retains CUDA ordinal0 regardless of the D3D12 adapter. Match adapter LUID/node and query actual multiprocessor count. Rebuild typed descriptor arguments with the exact matching ABI; never arbitrarily patch copied TMA descriptors.
- First-use preparation creates private queues/events and CPU waits up to10s. This is startup/first-shape work, not proof of a steady-state stall. Uploads/sessions are retained until process exit; safe bounded pools require actual queue/fence epochs.
- A replaced expansion may leave the original hidden tensor unwritten. On later contraction mismatch the source refuses partial fallback and requires restart. Unknown intervening functions can pass through while a pair is pending. Widening coverage therefore requires whole-segment dependency and completion-signal provenance.
- weights.bin is declared as37,765,120 bytes. Eight 1024x4096 and4096x1024 pairs contain67,108,864 slots:33,554,432 B FP4 data +4,194,304 B block16 scales +16,384 B for eight2048-byte cosine regions matches exactly. This verifies layout arithmetic, not weight values or quantization accuracy.

## Implemented

parameter_plan.hpp: host preparation primitive. Bind has no heap allocation, strings or map lookup; complete bounds/alignment/overflow checks precede any output modification. Bind into one preallocated storage instance per fence-owned workset. Do not use pointer patches for opaque TMA descriptor fields. The segment state model forbids replaying the original after replacement recording begins; it is NOT a live transaction implementation and cannot undo GPU commands.

profile_gate.py: paired full-NR GPU-wall-time analysis; rejects mixed identity, warmup, missing/duplicate pairs and impossible coverage. Coverage is the UNION of disjoint replaced regions. Independent paired runs, not correlated frames, are required. It never grants runtime admission or claims game FPS.

cutlass_sweep.py: orchestrates a locally built official CUTLASS79a for expansion/contraction shapes. Requires both a passed reference check and positive GPU runtime; success exit with an unsupported-GPU no-op is rejected. It is synthetic GEMM only, with the example's BF16 output; no original activation/layout/model behavior is validated.

## Validation

From experiments/nvfp4:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined test_parameter_plan.cpp -o /tmp/nvfp4_host_test
/tmp/nvfp4_host_test
python -m unittest -v test_profile_gate test_cutlass_sweep
```

Local Linux result:20,031 C++ assertions including10,000 repeat binds under ASan/UBSan;13 Python tests. No Windows/MSVC, CUDA, WARP, NVIDIA, game or GPU-performance verification. No new workflow or production hook is installed.

Build official CUTLASS79a separately, record exact CUTLASS SHA/CUDA version/SM120 target and GPU UUID/LUID/SM count, then:

```sh
python cutlass_sweep.py <local-79a-executable> --m 960 2160 3840 --output <local-results.json>
```

This is not an FP8 comparison or end-to-end benchmark. Add irregular M only after observing actual runtime tensor shape. Do not confuse SM100 data-center kernels with SM120 GeForce kernels.

## Optimization order

1. Metadata-only observation scoped to OUR Feature18/device/generation: exact hashes, chain lengths, tensor dimensions/strides, pointer roles, counter publication and disjoint GPU durations. No global interception of unrelated SR/RR/FG.
2. Move setup/validation off hot recording; cached host binding plans, correct LUID and queried SM count; bounded fenced pools. Integrate ParameterPlan only after ABI proof.
3. Replace captured contraction templates with a source-built SM120 block-scaled GEMM supporting validated dynamic M/strides. Padding must not change normalization neighborhoods, attention windows or visible extents.
4. Fuse producer quantization/scales, expansion plus its ACTUAL nonlinearity and hidden quantization, contraction plus the exact original skip/cosine/rounding/layout/completion publication. Do not assume GELU/SwiGLU from a kernel name. Do not batch eight sequential blocks as independent grouped GEMMs.
5. Remove IO kernels/barriers only after proving all consumers and memory-order requirements. Unsupported current metadata chooses the original BEFORE any replacement commands are recorded. After recording, stop new substitutions, retain dependencies, request F8 feedback/restart; never claim rollback.
6. Profile then calibrate other FFN/QKV/projections. Keep sensitive reductions, attention softmax and temporal state at higher precision until tests justify conversion. The goal is fastest accepted mixed precision, not an all-FP4 label.

Let T_region be the union time of replaced baseline operators; T_new includes quantization, all GEMMs, activation, repack, publication and sync. Delta_NR=T_region-T_new. An ideal2x speedup of a fraction p saves only p/2 of NR time before overhead. Full-frame benefit is smaller; report complete NR GPU wall time separately from summed kernel-active time and CPU recording time.

Quality gates: layer replay error/normalized L2/cosine/saturation/zero fractions followed by long temporal sequences, HDR exposure ramps, camera cuts, faces and disocclusion. LLM quantization results do not establish NR quality.

## Jitter

Current main forwards NR jitter keys and includes jitter mode in tuning identity. A Set/config log is not evidence that the runtime reads the key. Trace typed Get during Create/Evaluate, compare effective NR-side values and matched warmup/history/reset sequences, and inspect raw proxy/neural/composite output. No visible difference does not prove jitter irrelevant. Never modify game SR jitter for this diagnostic.

## Handoff boundary

This lab contains tested groundwork, not an enabled NVFP4 backend. Further work needs typed kernel/quantization source or independent reimplementation and real NVIDIA Windows validation. The ROG plugin was discoverable but its execution actions were not exposed; no ROG/GPU benchmark ran. Do not merge as a completed speedup, change core defaults, redistribute model payloads, or publish a runtime from this PR.
