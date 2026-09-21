# unified6 temporal memory/creation optimization candidate

Source-only isolated successor to `5567bb479275bf4eb24be4c8db5bbba9a11d5177`,
coordinated by #294 and intended for #300, then `next/0.5`. The existing
unified6 binary remains built from `cd4bc734`; this change is not that binary,
not a new OTA, and not a game-verified release. No game installation, model
replacement, NR-layer reduction, working-scale change or stable merge occurs.

## Reference and scope

The reference read and materialized for this task was MLX-DLSS
`0ca2deab092fe6f3e331bf4f616271dbc64521d0`, specifically
[NeuralRenderingFusedGlue.swift](https://github.com/iamwavecut/MLX-DLSS/blob/0ca2deab092fe6f3e331bf4f616271dbc64521d0/Sources/DLSSMLX/NeuralRenderingFusedGlue.swift)
(blob `24c28a60403d520d5ee4b363ee6df07e23a5b284`). Its useful principle is to
avoid unnecessary intermediate storage while retaining numerical semantics.
No Swift/Metal implementation was copied or translated, and no extracted
weights, NVIDIA DLLs or third-party runtime sources are included here. This
candidate optimizes our D3D12 stabilizer, not NVIDIA's internal NR network.

The authoring session's actual NewWin open_workspace invocation failed before
any host command with `FORBIDDEN: This conversation does not support developer
MCPs`. Plugin discovery found no replacement entry. Local checks therefore ran
in a Linux cloud container, not on the owner's Windows/GPU machine.

## Implemented A: immutable artifacts and live-device sharing

`recorded_temporal.cpp` now retains at most three immutable CPU bytecode
artifacts for fixed shader source/entry/macro combinations compiled into this
module. Concurrent creation is serialized; exceptions and empty artifacts are
not cached. Different module builds have separate static storage.

Root signatures/PSOs are shared by exact retained D3D12 device identity and
in-place variant, through four weak cache entries. An owning context keeps its
bundle alive; the cache alone cannot pin an unused device or GPU pipeline.
Saturation builds an uncached bundle instead of evicting a live owner. Once all
contexts release a bundle, later creation may rebuild its PSOs, although CPU
bytecode remains cached. No NR, residual or optical-flow history is shared.

`DLSS5_TEMPORAL_PIPELINE_CACHE=0` disables both caches for comparison. Caching
is enabled otherwise; the choice is latched on first use in each process.
The benefit targeted is repeated stabilizer creation/multiple live contexts,
not steady-state model execution or every cause of first-enable stutter.

## Implemented B: opt-in FP16 snapshot elimination

`DLSS5_TEMPORAL_INPLACE_FP16=1` requests a separately compiled pixel path. It
is OFF otherwise and latched on first use; it does not enable the stabilizer
itself. There is no saved INI/UI or ProviderV1 ABI change.

Admission requires the actual physical candidate format to be
`R16G16B16A16_FLOAT`, D3D12 additional typed UAV loads, format-specific typed
load/store capability, and successful optional pipeline creation. Unsupported
formats/capabilities or optional pipeline failure use the existing snapshot
algorithm. Capability failure is not interpreted as a valid FP16 fast path.
See [Microsoft typed UAV load contracts](https://learn.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads).

The shader loads the current pixel from its output UAV before storing that
same pixel; each thread has a unique target-subrect coordinate. Enhanced
neighbor pixels are never sampled. Reprojected original/residual data remain
in independent histories. Unused enhanced/candidate SRVs are bound null so no
SRV aliases the target UAV. A pre-dispatch UAV barrier publishes prior
compositor writes when the input is already in UAV state; the old snapshot
path's transition must not simply be removed without that ordering. Existing
post-dispatch publication and original states are retained.

On an available in-place context, the snapshot plane is initially omitted.
If a later input needs the legacy path, the snapshot is allocated before any
history transaction or AMD GPU work is recorded. This allocation rechecks
both the original context byte grant and the current process local DXGI budget
on the exact adapter/node-zero route. Unknown/insufficient budget or allocation
failure refuses the optional filter, retaining the current candidate and an
explicit F8 feedback reason. Failed allocation is latched, not retried every
frame. A successfully allocated fallback snapshot stays until safe context
retirement; it is not freed while older commands could still refer to it.

Creation admission conservatively still requires room for the full legacy
five/six-plane footprint plus the existing AMD allowance. The patch does NOT
relax budget admission, reserve the entire process budget, solve global NGX
allocation peaks, or establish a host-commit budget. #298 remains separate.

Internal `TemporalStats` adds snapshot bytes and path-recording counters for
the test harness. Availability/recording is not submission, GPU completion or
pixel adoption. These fields are not new overlay/F8 JSON fields in this patch.

### Cost accounting, not measured game savings

At 3840x2160, one RGBA16F plane's pixel payload is 63.28125 MiB. Omitting the
snapshot changes whole-chain five planes to four (316.40625 to 253.125 MiB),
and later-layer six to five (379.6875 to 316.40625 MiB), and avoids the
CopyCandidate dispatch. These are calculations for these planes only, not
measured residency or total plugin/game savings; AMD resources, alignment,
driver and other contexts are additional. A later fallback restores that plane.

## Intentionally unchanged

- All math, raw signed history, alpha/domain rules and user NR work settings.
- Submission/adoption/final fence, recording leases, cancellation and replay
  retirement code. The post-recording lifecycle functions remain unchanged.
- Original-history CopyResource. The proposed shader write fusion is deferred:
  half-to-float-to-half does not by itself prove preservation of all FP16 NaN
  payloads/special representations. Do not trade byte semantics for one copy
  without a separate tested change.
- AMD direct block-flow reads, GPU warmup/SCD, existing source-time weights,
  P1 reuse and supported routes. They already existed in unified6.
- No low-frequency/half-resolution histories, NVOF or new pre-SR/RR/HDR paths.

Future neighborhood-based processing must re-evaluate in-place legality.
The proof here is specific to current same-pixel candidate consumption.

## Verification completed in this task

| Layer | Result |
| --- | --- |
| Literal production portable C++ cache/policy section | 55 checks compiled/executed with Clang and GCC |
| Clang sanitizer run | AddressSanitizer/UndefinedBehaviorSanitizer passed |
| GCC independent broken-code controls | All four compiled, then failed at the expected oracle: duplicate compile, wrong device, omitted format gate, budget underflow |
| Legacy HLSL preprocessor comparison | Baseline and candidate tokens identical for standalone and normal RECORDING modes; NOT HLSL compilation |
| Structural source checks | Null alias views, pre-UAV barrier, preflight before history/AMD recording, full conservative admission, original-history copy retained |
| Python scripts | Syntax checked |
| Windows full Core / FXC / WARP / NVIDIA / game | NOT run; NewWin platform refusal above |
| Hosted CI | NOT validated; inherited billing-blocked run was not retried |

The first GCC harness attempt failed a misleading-indentation warning; the
harness braces were corrected without removing its assertion. One Clang
negative-control batch hit the tool time limit; it is not a passing batch.
The separate complete Clang sanitized positive run and GCC four-negative run
provide the results above. Failed/intermediate receipts remain outside source.
No benchmark timing or total VRAM/FPS improvement is claimed.

## Reproduction and required Windows follow-through

Portable check (native C++ compiler; output directory must be new):

```sh
python experiments/final_residual/validate_memory_opt.py --compiler clang++ --output <new-evidence-dir> --sanitize
python experiments/final_residual/validate_memory_opt.py --compiler g++ --output <another-new-evidence-dir> --negative-controls
```

The runner reads the literal production templates/policy, not a rewritten
mock. It obtains the baseline shader from the pinned Git ancestor or accepts
`--baseline-shader <file>` with its exact blob hash checked. No GPU pass is
inferred from preprocessing or CPU arithmetic.

The new Windows runner uses the existing recorded-temporal NVIDIA runner and
all its negative controls, instrumenting only isolated test copies:

```text
python experiments/final_residual/validate_memory_opt_gpu.py --sdk <clean-pinned-FFX1.1.4> --sdk-cache <aligned-object-cache> --compiler <clang++.exe> --output <new-external-evidence-dir>
```

It separately launches cache-off/legacy, cache-on/legacy and cache-on/in-place
processes, asserts actual path/capability use and allocated planes, and compares
14 native-motion result images byte-for-byte. Unsupported in-place capability
must fail this fast-path validation instead of silently passing via fallback.
The runner itself has only been syntax checked here; Windows compilation may
still reveal integration/compiler issues. It does not replace full Core gates.

Before integration/any player binary, run the complete
`scripts/build-unified-candidate.ps1` with the existing pinned dependencies,
full face backend and fresh output directory. Then validate actual Core WARP
and NVIDIA input/output subrects, prior-UAV writes, R8/R11/capability fallback,
FP16 special values, history/anchor alias rejection, budget failure, Reset,
replay, failed Signal and device loss. Compare supported same-workload games
for real-frame p50/p95/p99, creation time, peak and settled memory. Every bypass
must remain visible with F8/log feedback instructions. Do not install or claim a
performance candidate solely because the portable gate passes.
