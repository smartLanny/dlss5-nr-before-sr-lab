# Perf8R: paired-route lifetime, Reset and range-work integration

## Provenance and scope

Owner-supplied Perf7 BEFORE-SR and AFTER-SR captures identify shared residual
costs in source-state retirement/rebuild, command-list Reset/token lookup, and
range normalization. Main and open PRs were read before work; the research
integration target remains `next/0.5`, not main/stable. Do not merge or release
without a separate owner decision.

Base is Perf7 `a3ea8f9cbf3e06662e622852dc11581973798d18` (tested Core SHA256
`b72b5c3859fd288165414e5644e3d065edbba74102e354628aed517446d25d5d`). A separate
local Perf8 worktree was actively editing the same lifetime header; this work
was isolated rather than overwriting it. Its frozen commit
`0166c278a18b32e1a7e9255cdf5ad9ae6d34ae88` was reviewed and integrated, including
its lifecycle/range tests. Perf8R adds the separately validated Reset payload
change and bounded redundant-range folding. Its distinct name is intentional:
it must not be confused with another independently compiled Perf8 binary.

## Paired recording findings

Both captures identify the same Perf7 Core, one process and swap chain per run,
no repeated frame starts, and zero lost events/buffers. State transitions and
successful NR evaluations are corroborated by each F8's CURRENT PROCESS section,
not the preceding-process appendix or manual recording labels.

| Route | Initial OFF FPS | ON FPS | Later OFF FPS | Addon self CPU ms/s, OFF/ON/OFF |
|---|---:|---:|---:|---|
| NR before SR | 81.69 | 62.98 | 82.56 | 37.76 / 106.62 / 65.39 |
| SR before NR | 84.18 | 51.16 | 84.52 | 36.25 / 93.07 / 71.82 |

Controls are 2/2/2, one NR layer. Actual NR work sizes are 2228x1253 BEFORE and
3520x1984 AFTER; the AFTER source/output is 3840x2160. Do not label the latter
as a full 3840x2160 NR invocation. Different work sizes/routes are not a version
A/B or an equal-quality comparison. No identical-scene/clock/power guarantee is
inferred from matching configuration. Initial creation long frames remain in
both recordings, approximately 1.59 s and 1.84 s; they are excluded from steady
windows, not silently removed from the diagnosis.

Frame rate is 1000/mean frame interval, with whole intervals inside each stable
window. CPU uses explicit ETL UTC windows selected independently in those stable
states. No exact QPC join, CPU-ms/frame, or blocked-wait attribution is claimed.
Matched compiled-function IP intervals were aggregated across all addon samples;
those self times exclude descendants and are not proof of the whole-frame
critical path. Rebuild is about 8.70/11.32 CPU ms/s in BEFORE/AFTER ON windows;
Reset about 11.62/9.01; range introsort remains visible even before first NR.

## Integrated runtime changes

`nr_source_state_table.inl`: exact-key home/position indexes and a per-list
lifetime membership index remove retired members and repair only their probe
clusters. No rebuild of unrelated entries on each recording reset. Slots,
first-free admission, 256-entry capacity, no eviction, full recording/resource
keys and conservative rejection remain unchanged. All methods still require the
existing exclusive source lock. Full-address retirement keeps its exact predicate.

`nr_command_state.inl`: a bounded thread-local relative-slot hint accelerates
exact current-token lookup while the caller holds the existing registry lock.
Hits re-read the live token; no state/readiness/native identity or negative
publication is cached. Reset/Release still release every owned COM reference,
clear every root argument kind/value/mask and every dynamic field, and preserve
all original metadata/coverage/serial transitions. The already initialized,
inactive 64-word constant payload need not be overwritten for all 128 roots.
Restore and evidence readers must continue to honor kind and constant_mask.
Snapshots may copy those initialized bytes but must not replay inactive values.
Failed native Reset, retirement, queue/fence and actual native API ordering are
unchanged; there is no drop-update/try-lock optimization.

`nr_descriptor_provenance.h`: retain ordered/small/invalid-input behavior. A
bounded eight-pair shape probe permits pre-folding adjacent-in-input overlapping
or byte-adjacent same-type ranges. Missing a later overlap simply keeps the old
sort. Remaining large one-type, narrow-address ranges use the integrated exact
radix/reversal paths; scattered/mixed-type ranges retain comparison sort.
Validation precedes all mutation. Equal starts may be in any last-end order
because the final union uses max(last). Actual byte gaps, including stride gaps,
are never filled in by the merge. The complete normalization cost, including
classification/folding/scratch management, is covered by the timing fixtures.

`nr_descriptor_provenance_runtime.inl`: range arrays reuse existing reentrancy-
safe bounded TLS scratch blocks through the entire native-copy/UnknownCopy
lifetime. Allocation failure keeps the full conservative invalidation path.
Native game parameters are not altered; metadata locks do not cover the native
D3D12 call. New persistent memory is bounded; nested scratch use is tested.

No NR/SR/RR/FG routing, models, shaders, layer count, extent/subrect, jitter,
MV/depth/exposure or HDR policy is changed. Cold Feature18 initialization is not
moved to an arbitrary worker or hidden by OFF-state prewarming.

## Targeted checks already executed before full build

- Actual old/new Reset and setters: 10000 sequences including Reset/ClearState,
  partial bit-63 constants, metadata, null guards and balanced COM references.
- 20000 token slot mutations, miss-to-publication, retirement/reuse; 20000 reads
  across actual Windows SRW locking. Exact valid results equal the frozen code.
- In the synthetic reset workload all 8192 inactive constant words remain
  untouched, versus zero retained before; all kind/value/mask gates still reset.
- 16000 hot token lookups: 32136000 scan comparisons before, zero after warmup.
  Required live-token checks remain; these counts are not frame speedups.
- Integrated lifetime gate: 12000 mixed operations, 2000 live-table resets and
  40000 locked token lookups. Work budget 512000 -> 7194 hash/member/cluster
  checks, without changing semantics or admission/retirement behavior.
- 3000 exact normalization output comparisons, malformed input immutability,
  full-width addresses, mixed types and stride gaps; plus the integrated 300
  randomized unions and nested scratch tests. The first fold-only prototype
  added work to disjoint inputs and was narrowed before integration.
- Existing actual Barrier/identity suite passed, including source-lock side
  effects on pure UAV calls, 3500 mixed batches and 40000 native-SRW snapshots.
- Production state restoration WARP passed 24 scenarios. Deliberately corrupted
  restore constants were rejected by the GPU output oracle as expected.

Full candidate build results are recorded separately, after they actually run.
Local CPU, WARP, hosted CI, NVIDIA inference, and game verification are separate
facts. This document does not claim Perf8R FPS improvement.

## Build and packaging contract

`build-perf8r-candidate.ps1` includes all inherited SRW, Perf3..Perf8, canonical,
whole-Core descriptor/source WARP gates plus the new reset/range gates.
`package-perf8r-ota.py` requires their passed receipts and exact binary/source
hashes. No source-only or skipped-gate binary is marked install-ready.
The test package contains one Chinese Core, matching owned nrchain, licenses,
instructions and OTA metadata; never an INI, model or NVIDIA runtime replacement.

Next owner test is the same two routes, independently in fresh game processes,
OFF -> ON -> OFF with stable windows and same settings as their corresponding
Perf7 baseline. Capture F8 after timing. Verify successful NR counts during ON,
not only a requested toggle. Persistent bypass, missing effect, flicker or crash
requires rollback and F8/Core log/hash feedback; bypass-induced high FPS is not
an optimization success. Issues #277/#272/#273 remain open until real validation.
