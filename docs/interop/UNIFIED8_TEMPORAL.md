# Unified8 bounded temporal routes

This increment keeps stabilization OFF by default. Source generation and
synthetic D3D12 tests do not establish native game SR/RR, NVIDIA inference,
FG compatibility, or flicker quality. It does not change NR model settings.

## Production boundaries

| Entry | Backend | Scope | Additional proof |
| --- | --- | --- | --- |
| Native Feature1, after SR | Native MV / AMD OF | Whole / later layers | Existing packed native output snapshot and native-MV snapshot/units/phase contract |
| Native Feature1, before SR | AMD OF only | Whole chain, including a multi-layer chain | Known SDR Create flags; FP16 input exactly equals the complete active render extent; zero input base; actual final `combine_v2` output |
| Native Feature13, after successful RR | AMD OF only | Whole / later layers | Known SDR Create flags; existing RR success/generation/ownership gate; packed native RR output snapshot and existing actual post compositor |

The pre-SR seam runs **after** the real final compositor and **before** its
result replaces native SR's Color parameter. B is the unchanged original input;
Z is the actual composed result, at the full active render grid. Internal NR
work scaling does not change this temporal grid. No encoded model proxy is
substituted for B. The original Color parameter and base are still restored by
the existing native-call transaction.

The RR seam is in `FinishAfterSr`, reached by `ProcessRrPost` only after its
successful native result, matching live generation and ownership checks. It
passes an explicit route to temporal admission. Merely changing a Feature ID
at the old `Prepare` entry does not enable RR. Later-layer P1 uses the existing
primary-output descriptor table and compositor; no additional model Evaluate.

Both routes retain exact source identity, stable actual queue affinity, bounded
recording leases and the Core's Execute/Signal completion receipt. The module
borrows the caller's command list; it never submits or waits on the production
CPU. A submitted receipt is not a completed GPU fence. Unsubmitted Reset and
opaque NR replay remain subject to the outer Core quarantine rules.

OFF returns before domain inspection, snapshot/flow/history allocation or
temporal dispatch. Refusal preserves the current NR candidate. Completed but
replayable recordings retain their dependencies; OFF does not override them.

## Explicitly deferred

- Pre-SR native MV: copying vectors and checking units does not establish the
  jittered input raster's inter-frame sampling phase. AMD OF instead measures
  the actual B sequence. No synthetic zero MV is accepted as native proof.
- Pre-SR later-layer scope: the pre compositor's actual P1/field and descriptor
  selection needs its own nonzero equality oracle before enabling this scope.
- Pre-SR atlas/subrect, packed R8/R10, model-encoded input: no packed FP16 B
  producer in this increment. Matching work dimensions is insufficient.
- RR native MV: RR fallback/placeholder model guides do not establish temporal
  motion provenance or phase.
- HDR: the current accumulator admits `kResidualSdrCodeValues`, validates
  [0,1], uses a bounded photometric tolerance and saturates its accepted output.
  Linear HDR/PQ requires a named transfer/exposure contract for B/P1/Z, separate
  validated flow input conversion, unbounded residual/output math and numerical
  tests. Clearing the HDR bit or relabeling values SDR is not an implementation.
- Provider, private reconstruction, Present/FG-only and unknown ownership remain
  outside these native temporal entries.

## Validation entry points

`tests/temporal_route_test.cpp` exercises affirmative and negative route/domain
contracts on CPU. `scripts/validate-temporal-core.py` retains the whole-Core
continuous switching, exact queue/fence retirement and separate unsafe Reset /
replay quarantine regressions.

`scripts/validate-temporal-routes.py` compiles the entire generated Core plus
the verified compiled AMD/temporal module. Its fixture invokes actual
`PrepareBeforeSr`, `PrepareAfterSr` and `FinishAfterSr`, uses real D3D12 surfaces,
records known nonzero model edits, and observes actual Execute/Signal adoption.
The opaque model and native reconstruction picture are explicitly constructed;
this host does not claim a real NVIDIA model or successful native RR call.
Each scenario is a new process. OFF, HDR, native-MV refusals and pre later-scope
refusal must allocate no temporal history while preserving a nonzero NR result.

Final source/fixture hashes, per-case receipts and Windows/GPU acceptance must
be attached to the consolidated candidate. Private machine paths belong in
those receipts, not the public source or product UI.

## Bounded implementation evidence (2026-09-22)

- CPU route contracts: 20 checks passed.
- Complete generated Core WARP regressions: continuous switching 298,
  unsubmitted Reset 322, replay 329, submission/Reset overlap 327 checks passed.
- Actual entry host: pre-SR 100% and 75% work each processed nine source frames,
  recording/adopting eight temporal transactions after the first real NR
  submission established its queue/fence owner. RR post whole-chain matched
  this result. RR later-layer scope correctly waited for the independent
  second-layer Create fence, then recorded/adopted seven transactions in nine
  source frames (16 synthetic model invocations).
- Seven refusal/OFF scenarios passed with zero temporal allocation/recording:
  pre OFF, HDR, native MV, later scope; RR OFF, HDR, native MV. The current
  nonzero NR candidate was preserved.
- The host checked the complete generated codec SHA and all 26 cached O3 shader
  payload hashes before using them. The AMD/temporal module was rebuilt from
  the actual staged module files and its receipt was checked before linking.

Failed fixture runs are retained: an initial expectation incorrectly demanded
temporal work before a real queue/fence owner existed; another demanded P1
before the independent auxiliary Create completed. Corrected tests assert these
specific refusals and require the bounded transition to all eligible frames.
The shared queue helper's five-second WARP limit also expired with the device
still healthy during first execution of published O3 shaders; this host uses a
bounded 60-second actual fence wait, without a CPU completion signal. A cold
compositor probe explicitly disables the user reset-frame suppression option;
the production option/default is unchanged.

These are unfrozen overlay probes, not the final consolidated source receipt.
All GPU evidence above is WARP with constructed native/model pixels. No real
Feature1/Feature13/Feature18 model, game, HDR quality or FG claim is implied.

## Final frozen-source evidence

The complete `5c10a0b7` build reran all eleven actual-entry WARP scenarios and
passed. Its auxiliary creation oracle also passed with all nine precompiled
payloads byte-identical to runtime compilation, zero auxiliary D3DCompile calls
and eight retained PSO creations.

A separate RTX 5090 D host using that final Core, its own 18 objects, fixed
signed NR 310.8 and real native Feature1 then passed 1,567 checks over 64 source
frames. The actual pre-SR whole-chain AMD seam recorded and submitted 62 times;
history continued 61 times after one seed. The native callback consumed the
actual temporal result; the last source's temporal input and native O shared
the source/request/frame/recording/queue/final fence. The final NRO binary and
JSON both carried the exact built-source commit after a test-only macro fix.

This is one static artificial SDR scene with planar depth/zero native MV. It
does not establish motion quality, flicker reduction, ghosting, game adoption,
native Feature13 inference, FG pacing or HDR support. RR temporal still has
constructed-frame WARP evidence, not real game/Feature13 acceptance.
