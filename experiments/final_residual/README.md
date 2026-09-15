# Final residual history contract — next-batch experiment

This directory contains original CPU transaction policy, not AMD Optical Flow,
a temporal shader, or a feature in the uniform2 player package. No GPL source was
copied or translated into this helper. It does not change Core, INI or source pins.

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
records source/binary hashes. Four negative controls must compile and fail their
target assertions: wrong reference, mismatched OF generation, mismatched residual
generation, and cancellation after OF has become pending. No GPU/SDK is loaded.
