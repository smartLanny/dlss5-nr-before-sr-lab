# Unified render / NR / FG control model

Relates to #13, #16, #4 and #7. This branch is stacked on `codex/research-fg-mfg-control-v1`. Review the FG branch first; retarget this PR to the maintained beta branch only after the parent merges. Do not merge the full product-manager tree into the core.

## Implemented slice

`src/research/unified_controls.h` provides an independently testable request/view-model, a fixed-point scale planner, and an observation-confirmed scale transaction. It combines the FG policy with render/NR status, but is NOT wired to ImGui, NRUI exports, ApplyConfig, game CVars or NGX. No game-visible menu or real render controller is added in this commit.

Definitions: O = output size; R = true game SR input size; N = NR work size. Render scaling is R/O, NR scaling is N/R. `renderPermille=0` means leave R to the game. 750 means 75% of each dimension, not 75% of pixel count. The planner allows N control with game-owned R and explicitly rejects a true-R request when no verified controller exists. It does not mistake Feeder/backbuffer downsampling for reduced game rendering.

The initial planner supports NR 50%-100% and render 25%-100%, with conservative integer alignment on R. These are research policy ranges, not guaranteed game/runtime ranges. A production controller must validate its exact supported sizes and N allocation constraints too. DRS ownership, F8 capture lock and restart-only changes return explicit status; output observations are not inferred from the requested values.

## Transaction semantics

A request alone cannot update applied dimensions. Observe a newer source frame with matching R/O, verified same-frame guides and a new monotonic resource/mapping generation when R changes. `resources` is a logical generation, not a texture pointer and not a required physical reallocation: a proven change of the active scene region is sufficient to advance it. Padded physical allocations may remain unchanged.

After successful observation, ask the existing NR/guide manager for the required reset, then call `CommitNr` only after a successful NR evaluation. Failed evaluations retain pending state. Output changes cancel; timeout is 120 source-frame advances, including observations whose guide validity has failed. It is not a wall-clock timeout; runtime shutdown/paused-source cases must explicitly cancel. Context, provider, exposure or camera-cut resets still belong to the existing guide/history logic; `NeedsHistoryReset()` only adds scale-change evidence, not a replacement for all reset reasons.

The class is single-control-thread, with no GPU resource ownership. `AppliedRender/AppliedNr` are the last successfully applied values, not proof they are still current after a provider/output/session change. Invalidate their UI freshness externally; recreate the transaction on session/provider changes. Serialize backend commands and actual-frame observation; never call `CommitNr` using a cached ready state from another frame. Pending render changes need a backend-specific rollback/recovery policy not implemented here.

A menu request is not an atomic multi-backend transaction. A future executor must suspend owned FG before an actual R/O change, apply the game controller, observe new guides, reset NR/SR, warm up, and only resume FG after valid input/pacing evidence. This model does not automatically perform those steps or promise all controls hot-switch.

## Proposed menu, once runtime adapters exist

- Rendering: output O read-only; actual R read-only plus Game/validated quality settings; requested, applied, restart-pending or rejected reason.
- Neural rendering: existing mode/strength/style/automask plus independent actual N and N/R. Reuse existing behavior; do not duplicate NR work-scale implementation.
- Frame generation: Follow game / Off / supported fixed multipliers / Dynamic only when validated. Show native, experimental external Ada and hybrid outputs separately.
- Diagnostics: active provider, original guide provenance, real source rate vs display rate (when measured), stage GPU timings (when available), ownership and bypass reason.

The existing Draft PR #6 pins the older `e0b94ff` core. Our maintained base is `175173f` / beta0.4.1-r2. Re-audit and adapt that wrapper against the exact current core before wiring this model. Keep NRUI V1 layout/export behavior stable; add a versioned extension with explicit struct size and capability revision rather than append fields to V1 in place. Retain the old overlay fallback and F8 locking behavior. This branch does not imply that PR #6's old binary is compatible with r2.

## Local validation

```
cmake -S tests/research/controls -B build/research-controls
cmake --build build/research-controls --config Release
ctest --test-dir build/research-controls -C Release --output-on-failure
```

GCC Release/CTest and Clang ASan/UBSan pass: 1,038 CHECK evaluations, including scale sweeps, failed NR, unchanged settings, stale frame, unproven resource change, output change, invalid observation and bounded timeout. The dependency's FG suite also passes. These tests do not exercise the real add-on, Windows, game resources, GPU synchronization or on-screen menu.
