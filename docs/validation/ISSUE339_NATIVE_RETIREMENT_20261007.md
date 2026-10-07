# #339 native recording retirement evidence

Status: development reproduction; behavior fix and release validation pending.
The original guest feedback lacks holder identities, so this is a demonstrated
production lifetime defect, not a confirmed explanation of that guest session.

## Exact failing sequence

The existing whole-Core queue harness now includes two cases:

- `native-final-destruction`: register a native-created command list without a
  ReShade wrapper, record its NR context/slot ownership, submit an empty WARP
  list, wait for its exact production queue fence, retain a raw native reference,
  then destroy the final native reference without Reset.
- `native-marker-replacement`: the same sequence, additionally replace the
  private observation marker while the raw list is alive. Marker replacement
  must not retire NR ownership; true final destruction must do so.

The registration metadata is controlled fixture input. Native COM objects,
the lifetime marker, `RecordNrUseLocked`, submission/Signal, fence predicates,
collector and drain are production code. No shader, NR/NGX model, ReShade
game host or game runs. The carrier is an identity only, not a model handle.

Before final destruction, the exact fence is complete but the list can replay;
the original predicate correctly blocks release. After final destruction, the
old Core removes its command-state registration but retains the NR ledger:

```text
NATIVE_NR_BEFORE_FINAL stateToken=1 token=1 recording=1 slot=1 pending=0 fence=1 completed=1 markerReplaced=0
NATIVE_NR_AFTER_FINAL stateGone=1 ledgerPresent=1 recording=1 slot=1 retiredLive=1 pendingDestroy=0
FAIL NATIVE_FINAL_DESTROY_MUST_RETIRE_NR_RECORDING
```

Both old-source cases exit **41 at this assertion**. The raw-reference retention
and marker-replacement refusal assertions pass first. No direct detach or manual
recording-counter adjustment is used. `NativeCommandLifetimeRelease` consumes
the state registration only; successful native Reset and wrapper destroy are
the existing NR recording consumers. Ownerless native destruction lacks a
paired NR consumer.

## Source identity and rerun entry

Executed baseline: generated full Core
`8d89f8e8269b9040aca7d6c55f91a8aad683fff2`, source SHA-256
`017b088976d7884b17f401427ff55bcda086ab3fd1f55182c0f46246b583b8b3`.
The original generated source and verified temporal/private module objects
were read in place and not modified. This is not a new main or candidate build.

The following functions, plus the entire `nr_native_command_record.inl`, were
compared individually and are identical to main
`024f9cc32778c1056db364d76d087ee257da6502`:
`CanDestroyContextLocked`, `RecordNrUseLocked`,
`DetachLifecycleCommandListTokenLocked`, `CollectRetiredContextsLocked`,
`DrainPendingContextDestructions`, `CompleteSubmissionBatchLocked`, and
`OnDestroyCommandList`. This does not establish complete-tree, algorithm,
runtime or game equivalence.

Use `scripts/validate-queue-retirement.py` with its existing `--compiler`,
`--reshade`, `--source` and fresh `--build-dir` arguments. The source must be an
accurately identified canonical staged Core with its verified module receipts.
Add `--native-destruction-negative` only for an unfixed baseline. It requires
both precise runtime exit41/assertion failures, not an arbitrary build or test
failure. Without that option the standard queue matrix requires both cases to
exit0; a future fix must also fail again when removed.

Private local receipts retain actual commands, source/fixture/module hashes and
complete stdout. They contain local paths and are not copied into this report.

## Destruction proof and ownership review boundary

A separate Windows 11 WARP object-only probe passed 10 checks: native list QI
for `ID3DDestructionNotifier` succeeded; overwriting its observation marker
destroyed that marker without notifying native destruction; an extra native
reference prevented notification; final Release notified exactly once. It
created no queue and submitted no GPU work. This proves that local WARP
contract, not support on every driver/Agility version.

Microsoft documents that [private-data interfaces also release on
replacement](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12object-setprivatedatainterface).
A [true destruction callback must not access the dying
object](https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/nn-d3dcommon-id3ddestructionotifier).

The proposed seam, submitted for maintainer review before ownership changes:

1. Separate marker IUnknown references from the notice's notifier/NR POD
   lifetime. A callback reference in the marker's existing reference count
   would incorrectly defer state retirement on marker replacement.
2. The true native callback publishes only a captured-token `native_dead`
   atomic flag and drops its own POD reference. It must not access the dying
   list, acquire Core locks, collect/drain, call NGX or release models.
3. The existing collector, already under the existing chain lock, consumes
   only the matching native/NR generation and returns the original recording
   references. It must not query or clear private data on the dead list.
4. Successful Reset consumes only its old recording token; wrapper departure
   alone cannot prove a retained native list dead. Unsupported/unproven
   notification, failed Reset, pending submission, wrong queue/fence,
   device removal and busy binding/private/temporal owners retain refusal.

There is no implementation of this new ownership seam in this evidence commit.
No overlapping Create, counter clearing, timeout-based release, pool expansion,
runtime change, game installation or algorithm change is authorized by the
reproduction. Preserve the original all-consumer retirement and outside-lock
vendor release contracts.

## Remaining verification

S1 must report the original first blocking branch, full bounded read-only
blocker evidence and explicit unknown identities without extra Ready/Invalidate
calls. The approved minimal event fix must turn the two causal cases green and
retain wrong-event refusals. Full bilingual canonical validation and actual
game acceptance remain pending; old negative-control success is not a fix.

Execution specifications: [#339 runbook](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/339#issuecomment-6034176166),
[#309 dispatch](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/309#issuecomment-6034217346).
Review coordination: [#311 ownership seam](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/311#issuecomment-6035682050).
