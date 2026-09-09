# #205 initial recording candidate

Current owner handoff: the dev15 fix is implemented and installed in Death
Stranding Director's Cut, and is inherited unchanged by `0.4.7beta` (#217).
The installed dev15 SHA256 still matches its deployment receipt. The owner's
new dev15 F8 proves the first-recording state is now admitted, but queue affinity
remains unverified and NR/submission counts stay zero. The game is still failing.
The owner explicitly paused #205 to prioritize the integrated Manager package;
no queue-policy change was made and the issue remains open. The sections below
retain the original candidate and validation/deployment history.

Base: `b5a17cbf0531f7d5fa3e35d0b65df054aa12aa2d`, isolated
`codex/ds1-first-recording-20260909`. Initial candidate identity:
version beta0.5-dev15 / PE 0.5.0.15. Final deployment and validation follow below.

The historical Death Stranding `ds.exe` hotfix.1 F8 has 12 distinct freshly
created DIRECT carriers with `createMethod=1`, `initialPso=0`, 42 observed PSO
writes, zero native Resets and one capture each. Native creation registration
and real Reset recovery already exist in the base. They do not cover a list
which completes its only recording without Reset.

## Admission and revocation

The first actual native-created, initially-null carrier requests complete
observation coverage. That existing recording remains unknown; publication does
not reconstruct previous bindings. Only a subsequent real CreateCommandList
return, before application recording, may start first-recording observation.

The return observer requires an authentic creation receipt, matching private
object/device identities, attached native COM lifetime, no previous state,
recording/capture/Reset notification, and an already published full-guard family.
The existing family matcher proves the device's covered meta domains, vtable and
complete per-object QI interface set. It reuses the bounded 128-entry observation
pool and cannot install new hooks from the creation callback. Missing capacity,
family, meta or lifetime proof leaves the recording unknown.

A successful receipt and complete observation still leave `known=false` with
`create-awaiting-PSO`. Only a subsequently observed ordinary non-null
SetPipelineState establishes the distinct `Create-first-recording` baseline.
The initial dummy PSO is never restored. Initial non-null PSO and
CreateCommandList1 cases are deliberately outside this first implementation;
they retain actual Reset/ClearState recovery.

No Reset is synthesized: native Reset counters, reset serial and null-guard
freshness remain zero/false. Full-guard observation covers unsupported recording
operations as well as supported bindings. Unknown/invalid state changes keep a
reason that a later PSO setter cannot clear. In particular, malformed
heap/root-argument writes now record an explicit invalid reason instead of
leaving the pending-first-PSO reason behind. Genuine Reset/ClearState clears
first-recording provenance and follows the existing implementation. Native
lifetime revocation/destruction and pool eviction destroy the evidence.

The existing capture/restore predicate, meta dirty-state checks, queue affinity,
and submission/fence/lifetime protections remain in force. This only adds a
proven start of observation for ordinary PSO state; it does not authorize
unknown state or broaden dummy/null PSO restoration. Roots, heaps and arguments
are tracked from the actual creation boundary; ordinary restoration does not
ClearState or invent unobserved dynamic defaults.

F8 exposes `commandState.firstRecordingObserved` and the corresponding history
field alongside baseline, native Reset counts, resetSerial and freshReset.
`firstRecordingObserved=1` records where observation began; it is not admission
by itself. Reason/captured and existing submission counters remain necessary.

## Focused validation

CPU commands use the existing LLVM-MinGW compiler with
`scripts/validate-first-recording.py`, `validate-command-creation.py`,
`validate-reset-notification-gap.py` and `validate-core-feedback.py`.

- Actual first-recording, family matching/priming and pipeline observer functions:
  57 checks passed, including 20 fault cases, no fake Reset and sticky unknown
  state. Removing the early observation reproduces the expected negative control.
- Existing creation observer: 61 checks passed.
- Existing real Reset result boundary: 28 checks passed.
- Actual F8 coordinator/serialization: 159 checks passed; first-recording source,
  zero resetSerial and false freshReset are checked together. No GPU seam ran.
- Full native D3D12/MinHook harness compiled with warnings-as-errors in BuildOnly.

The dedicated `--first-recording` native harness starts from an empty family:
first real Create/carrier refuses and publishes coverage, then subsequent real
Create(NULL)/setters/Capture/Restore are exercised without a native Reset. It
also checks lifetime revocation and a separate actual Reset recovery. The
harness uses WARP D3D12 objects without NGX/model calls or GPU queue submission.
The initial dedicated API run passed 34 checks; its negative control reproduced
`FIRST_RECORDING_NATIVE_ORACLE` (exit 41). The final frozen acceptance reruns it,
including native heap and both-root restoration assertions. It does not submit
GPU work and is not a GPU execution pass. The wider GPU/WARP suite remains
explicitly unexecuted without RunWarp; the packager continues to require it.

Real game acceptance remains a fresh `ds.exe` F8 showing first-recording origin,
Feature18/NR successes and protected submission progress in both requested
modes. Neither the old F8 nor CPU checks establish compatibility or image quality.

## Final local validation and owner deployment

Build source `3580e8d35531e47ecf55d9452c987068303fc253` passed all 36 acceptance
checks, including bilingual compilation, the complete native state/lifetime
matrix, wrong-restore controls, eight production RR depth WARP cases and the
existing composition WARP fixture. The first-recording API fixture itself does
not submit GPU commands; the separate WARP fixtures provide GPU execution checks
using constructed neural planes, not NVIDIA NR inference.

The previous full run at `da3b048` failed its legacy native fixture. An unrelated
post-publication Create in the missed-init case now legitimately published full
guards, invalidating five old timing assumptions; cold-pool and missing-reset-
notification cases were also no longer cold after enabled Create observation.
The fix changes only real fixture event order: both legacy missed-init objects
are created before observation, and cold Reset cases create while NR is off then
enable before the real Reset. All 65 original safety Check expressions remain
unchanged; no production admission was relaxed. Native stdout/stderr and actual
exit codes are now captured explicitly. The failed run is retained, not relabeled.

| Final artifact | SHA256 |
| --- | --- |
| Source manifest | `35b50a13767029941cfa40d0d4418cbea0c57e1ef0593ba90110308547b21cf6` |
| Validation record | `bc8b6282f5c1b0796cddcf6a7d8b1d383ad72bcb76c5b44bdd0d28ea90d4ded0` |
| Chinese Core | `49fea7d7922d4f91dcd6f2b69652147434cfda9c5c28523839c58ab117ef2a0a` |
| English Core | `e2a72a76c9566462738dbe35288a452649db16b6e13ff8c61f9775ed07cac070` |
| Owned companion | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

Only the verified Core was deployed to the owner's Death Stranding Director's
Cut (the first game, `ds.exe`). Existing settings, ReShade, vendor runtimes and
other DLLs were checked unchanged; the previous Core and restore receipt remain.
Owner game acceptance, NVIDIA inference and performance remain unverified.
The frozen production diff received one runtime-safety and functional-contract
review; the fixture/logging correction received a targeted recheck. No P0/P1
finding remains from those reviews. No main merge or stable release is implied.
