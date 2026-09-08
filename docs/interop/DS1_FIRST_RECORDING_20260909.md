# #205 initial recording candidate

Base: `b5a17cbf0531f7d5fa3e35d0b65df054aa12aa2d`, isolated
`codex/ds1-first-recording-20260909`. This is a local uninstalled candidate;
version beta0.5-dev15 / PE 0.5.0.15. No release, push or game result is implied.

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
