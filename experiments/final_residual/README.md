# Final residual history contract — next-batch experiment

This directory contains original CPU transaction policy and an isolated, actual
AMD FidelityFX 1.1.4 D3D12 optical-flow producer. It contains no temporal pixel
shader and is not a feature in the current Core package. No GPL source was
copied or translated. It does not change Core, INI or source pins.

### 2026-09-21: bounded retirement implemented

The standalone producer now reserves one of four context slots **before** GPU
allocation. Its destructor transfers pending work into that bounded registry;
it no longer abandons an untracked allocation on each settings change.
`begin_retirement` accepts the exact last-consumer completion as a terminal
handoff. `collect_retired` polls without waiting and destroys only after both
producer and consumer completion. Unpublished submission, absent consumer proof,
device loss or a failed vendor destroy keeps its slot quarantined. Capacity
exhaustion refuses the new configuration; it does not lower the work size.

Verified on RTX 5090 D / driver 616.92 with actual FidelityFX 1.1.4: 57 real
dispatches, 1,544 checks, two four-context pending/complete cycles, refusal of a
fifth context before GPU allocation, and a pending consumer fence after the
producer already completed. Both existing wrong-direction/scale controls were
detected. Debug layer unavailable; no actual device-loss or failed-Signal
injection, Core integration, game denoising or VRAM-savings claim.

GPU probe SHA256:
`279aac63effdce05c278fba6ab7b9d1df1c7438db09c0595098ee6c88edea007`.
Full build, hardware log and readbacks remain in the private evidence archive.

## Verified upstream reference

The actual repository is [SAOG0721/Magpie](https://github.com/SAOG0721/Magpie).
`SAOG0721/MagpieMagpie` did not resolve. On 2026-09-16, official GitHub refs were:

- `v0.6.8-experimental`: `ec30218c207394e30569694fd003c42d59620337`.
- `experimental`: `3841698348bfb246623d4acf791984c8b68a577b`.

Official GitHub tree/blob responses confirm the relevant temporal/provider files
have identical blobs at those two points. Relevant fixed source:

| File under the fixed tag | Verified role |
| --- | --- |
| [DLSSNRMultiPass.h](https://github.com/SAOG0721/Magpie/blob/ec30218c207394e30569694fd003c42d59620337/src/Magpie.Core/DLSSNRMultiPass.h#L64) | Chain-final hook; multiple passes use **first-pass output** as the residual baseline, while one pass uses original input. |
| [DLSSNRTemporal.cpp](https://github.com/SAOG0721/Magpie/blob/ec30218c207394e30569694fd003c42d59620337/src/Magpie.Core/DLSSNRTemporal.cpp#L114) | D3D11 resources, current-to-previous source-pixel flow, duplicate suppression and history dispatch. |
| [DLSSNRTemporalShader.h](https://github.com/SAOG0721/Magpie/blob/ec30218c207394e30569694fd003c42d59620337/src/Magpie.Core/DLSSNRTemporalShader.h) | Signed residual accumulation, conditional support/amplitude accumulation+, and half-resolution low-frequency observations with current high-frequency complement. |
| [DLSSNRTemporalState.h](https://github.com/SAOG0721/Magpie/blob/ec30218c207394e30569694fd003c42d59620337/src/Magpie.Core/include/DLSSNRTemporalState.h) | Capture time/revision/generation bookkeeping; 80 ms EMA and a 250 ms maximum gap. |
| [AmdOpticalFlowProvider.cpp](https://github.com/SAOG0721/Magpie/blob/ec30218c207394e30569694fd003c42d59620337/src/Magpie.Core/AmdOpticalFlowProvider.cpp) | Actual FidelityFX dispatch through D3D12/D3D11 interop; not the accumulation algorithm. |

Magpie's [LICENSE](https://github.com/SAOG0721/Magpie/blob/ec30218c207394e30569694fd003c42d59620337/LICENSE)
is GPLv3; those files have no separate permissive license notice. Their C++/HLSL
are research references, not vendored implementation here. AMD FidelityFX SDK
v1.1.4 has its own [MIT-style license](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v1.1.4/LICENSE.txt).
The two licensing scopes must not be conflated.

Upstream mode 2 accumulates signed RGB residual with reprojected original-color
patch rejection. Mode 3 separates support from conditional residual amplitude
(history alpha is `1 + support`), so missing observations do not simply insert
zero residual into the amplitude average. Mode 4 first forms signed 2x2 residual
and original-guide observations, reconstructs their low-frequency component with
color guidance, accumulates that component, and retains the current high-frequency
complement. History alpha is validity/support, not source opacity; final alpha is
taken from the current raw result. Those distinctions require separate pixel tests.

The D3D11 upstream binds eight SRVs (original/base/raw, previous residual/guide,
motion, low residual/guide), three UAVs (output/next residual/next guide), one
sampler and 48 bytes of constants. Residual and guide histories are each a pair
of full-resolution RGBA16F textures. Mode 4 adds two half-resolution RGBA16F
observations; it **does not** reduce the full-resolution history allocation.
Four full-size history planes alone cost about 253 MiB at 3840x2160, before flow,
output, low observations or SDK allocations. A memory-budget check is required.

Magpie's project file compiles OF from `$(FSR3SdkDir)/Kits/FidelityFX/...`, using
the `FfxApi` host layout. Its public build options leave the SDK root unspecified;
the redistribution document lists a local FSR-SDK-v2.3.0 cache, not a pinned
source revision. That layout is different from PR256's explicit FidelityFX
SDK v1.1.4 `sdk/include/FidelityFX/host` dependency. Do not mix their headers,
backend source or generated shader blobs.

## Executable implementation boundary

The user's target is **entire-chain residual**: baseline is the current original
canonical color, raw result is the completed chain after its composition controls.
The intended operation is original plus stabilized signed `(raw - original)`.
This deliberately differs from Magpie's multi-pass `(raw - first_pass)` baseline.
Never feed the stabilized output into NGX or the next optical-flow input.

For a first D3D12 Present implementation, the insertion point is after the final
`Codec(..., true)` and its UAV barrier and before `DrawBackbuffer`. Existing
`image.source` is the original canonical base and `image.result` is raw. Separate
output storage is needed to avoid reading and writing one image simultaneously.
Start with a declared SDR input contract; the existing OF probe quantizes its
own immutable input to RGBA8 and is not an established HDR mapping.

Prior inputs that can be reused without copying Magpie implementation:

- PR [250](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/250), head
  `36aeae2b8b8288e861657993995ca6265e00450e`: guide-only AMD/NVOF probe.
- PR [256](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/256), head
  `4a931d59a812d407c86725fb90b088dc9e05d412`: project-owned
  `experiments/fullres_guides/backend.cpp/.hpp`, `geometry.hpp`, `policy.hpp`,
  `shader_source.hpp`; FFX1.1.4 static backend with accepted-frame bookkeeping.
  Its Session currently invokes ExternalProviderV1 and commits after Core
  writeback completion. A final-residual consumer needs a separate OF-only
  delivery interface, not that preview result or existing Core submission loop.
- The local provider archive's backend blob was verified as
  `0eaa059ae58222295c62c041cb7cc0b810f08c78`, matching PR256's current blob.

The AMD path must call actual `ffxOpticalflowContextDispatch`, retain one consistent
SDK/header/library version, and return exact current/reference source IDs. The
existing backend uses 8-pixel-grid `R16G16_SINT` flow expanded to source-pixel
`R32G32_FLOAT`; Magpie expands to `R16G16_FLOAT`. A conversion and direction/scale
fixture is required before connecting either to a consumer. Magpie's fixed 0.65
confidence is a heuristic, and its allocated SCD resource alone is not evidence
of scene-cut feedback into the temporal filter. Do not copy its blocking
D3D11/D3D12 flush/wait/destructor behavior into Core's queue/fence lifetime model.

## This helper's contract

1. `prepare(frame)` runs **before OF**. Only a unique current real source can
   receive a token. It returns either `ResetBoth` or `Continue` plus the exact
   accepted reference. Consecutive source IDs, monotonic source time, matching
   device/config/color/grid/backend and no explicit scene reset are required
   to continue. A skipped real source forces reset; duplicate FG displays do
   not rotate either history.
2. `observe_flow(token, receipt)` validates actual backend, current/reference,
   readiness and reset acknowledgement. `history_generation` is a logical
   reset epoch shared by both histories, **not** a texture address or SDK context
   handle. The adapter must acknowledge it only after performing the vendor
   reset and scheduling the matching residual-history reset. Pending work cannot
   be cancelled as pre-OF work.
3. `finish(token, residual_generation, outcome)` advances the accepted reference
   only for a submitted and adopted result in the same logical generation.
   If OF advanced but residual did not, both histories become invalid and the
   next frame requires a new reset epoch. A mismatched generation cannot be
   repaired by merely relabeling a texture.
4. Device removal or uncertain/failed submission quarantines this state object.
   It never declares resources retired. Reconstruction requires the caller's
   proven device/resource retirement, not resetting this C++ object in place.
   Stale callbacks cannot mutate a newer token.

The adapter still owns texture formats, finite pixel checks, motion convention,
resource lifetime, queue waits/barriers, actual SDK calls, source timestamps,
long-pause detection, budgets, and pixel-history ping-pong scheduling. `Ready`
must come from the real producer's execution-order proof. Passing an enum cannot
turn ordinary game MV into AMDOF. Temporal weights and pixel math are intentionally
outside this helper.

## CPU validation

Run `python experiments/final_residual/validate.py --compiler <clang++> --output <external-directory>`.
The runner freezes the actual header/test, checks strict C++17 compilation and
records source/binary hashes. Five negative controls must compile and fail their
target assertions: wrong reference, mismatched OF generation, mismatched residual
generation, cancellation after OF has become pending, and ignoring unavailable
motion during OF warmup/SCD. This CPU test does not load a GPU/SDK.

## Actual AMD OF producer: isolated hardware experiment

`amd_of_producer.hpp/.cpp` removes the PR256 Session's ExternalProvider Submit,
Core writeback, depth, NGX and NVIDIA backend dependencies. The project-owned
area reduction and block-to-source conversion in `amd_of_shader.hpp` come from
PR256; **they are not the AMD optical-flow algorithm**. Actual optical flow calls
`ffxOpticalflowContextCreate/Dispatch` from the official
[FFX 1.1.4 source](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55),
commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`.

### Build and dependency boundary

`build_amd_probe.py` requires an unmodified SDK checkout at that commit and an
explicit compiler path. SDK files/binaries stay outside this repository. It
uses the SDK's own `FidelityFX_SC.exe`, `dxcompiler.dll` and `dxil.dll` to generate
all 28 Wave32/Wave64/16bit optical-flow permutation groups, then compiles the OF
component, DX12 backend, shader blob accessors and shared support together.
There is no mixed-version prebuilt SDK library. SDK root license is MIT-style;
bundled Microsoft compiler/runtime tools retain their own SDK license files.

LLVM-MinGW requires forced standard Windows/wide-character includes and Unicode
definitions. The build creates an **external copy** of `ffx_dx12.cpp` that removes
only `#define ENABLE_PIX_CAPTURES 1`, disabling optional PIX instrumentation whose
headers reject non-MSVC builds. It preserves the original copyright/license and
hashes both versions. It changes no optical-flow shader, algorithm or ABI.

Run:

```text
python experiments/final_residual/validate_amd_probe.py --sdk <official-1.1.4-checkout> --compiler <clang++.exe> --output <external-evidence-directory>
```

The runner freezes the compiled project source, records exact commands and
SDK/compiler/binary hashes, runs actual RTX 5090 D hardware, saves every complete
RG32_FLOAT flow plane and three-word SCD readback, and hashes the logs/readbacks.
Two additional **real GPU** negative controls reverse the motion direction or
remove the 50% source-unit scale. Both must compile and fail the direction/unit
assertion. No WARP or CPU optical-flow substitute is permitted.

### Source pair, queues and availability

- Constructor accepts the caller's same-device DIRECT queue. A single serialized
  owner supplies an immutable private original SDR RGBA8 texture already ordered
  on that queue, in COPY_SOURCE. This is not a game-resource capture interface.
- Source/plan mismatch, wrong reference, invalid backend/extent, stale token or
  older source is rejected before the SDK call. A repeated source is a duplicate,
  without another dispatch. One result remains occupied until explicitly retired.
- 100% or 50% is explicit. 50% uses area-overlap filtering and ceil-half extents.
  SDK flow is grid-8 RG16_SINT; exposed vectors are full-source RG32_FLOAT in
  **current-to-previous source pixels**, in COPY_SOURCE. Source extents above 4096
  on either axis, or OF inputs below 128, are outside this probe's contract.
- `poll` requires the exact producer fence before returning Ready and mapping
  the producer's tiny SCD readback. It retains the original source through output
  retirement. The caller must submit all actual output reads and provide their
  same-device, completed fence to `release_output` before any reuse.
- The API is deliberately CPU-polled for this standalone experiment. A production
  consumer should evaluate availability in its ordered GPU work; this experiment
  is not evidence of acceptable in-game synchronization cost.

**Ready does not imply usable motion.** Official
[FFX callbacks](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55/sdk/include/FidelityFX/gpu/opticalflow/ffx_opticalflow_callbacks_hlsl.h#L533)
disable optical flow for `FrameIndex <= 5` after every reset and while any of the
low four SCD history bits are set. The actual first six source frames therefore
produce zero flow. The producer exposes `motion_usable=false` during that period
and during actual scene-change suppression; SCD words are histogram divergence,
history bits and completed workgroups, not per-pixel confidence.

`HistoryContract` now returns `ReseedResidual` for completed, correctly paired OF
whose motion is unavailable. A future pixel consumer must write the current
residual as its new history **without accumulating previous pixels**. This lets
vendor warmup progress; resetting OF on every unavailable frame would prevent it
from ever becoming usable. Nonadoption, mismatched generations, and uncertain
submission retain their original reset/quarantine behavior.

### Hardware scope and limits

The deterministic fixture uploads 512x384 nonperiodic textures translated by
(+4,+2) original pixels per source. In each 100%/50% run, usable frame pairs produce
(-4,-2) vectors throughout the measured interior with zero endpoint error. Each
run dispatches 24 real sources and tests 24 duplicate submissions; only 24 SDK
dispatches occur. It also covers actual OF advancement followed by deliberate
nonadoption, explicit scene reset, warmup recovery, and white/black cuts whose
actual SCD history values progress through 1, 3, 6 and 12.

These are synthetic hardware OF checks, not image-quality approval. Residual
adoption in this fixture exercises the CPU protocol; no residual pixel shader
is present. A first periodic sinusoid fixture produced large aperture/periodicity
ambiguities and failed. A nonperiodic multiscale fixture replaced that ambiguous
test image; the direction/unit thresholds were not relaxed. Both early failures
are retained in external evidence. The tested driver is 616.92; the system's
D3D12 debug layer was unavailable, so no debug-layer pass is claimed.

**Lifecycle boundary before Core integration:** the destructor conservatively
retains state on a held result, an executed list without a published Signal, or
an incomplete/removed-device fence. The current mechanism releases the C++
`unique_ptr` ownership and intentionally leaks that state until process exit.
This prevents premature destruction in the isolated probe. It is **not** a full
retirement queue or a game-ready lifecycle. Core integration requires an explicit
retirement owner, bounded quarantine/accounting, failure/teardown tests and a
proven capture/consumer fence contract. This source does not implement those
production responsibilities or modify the current Core package.
