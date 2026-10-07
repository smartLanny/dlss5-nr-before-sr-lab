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

## S1 diagnostic implementation

The original collector now publishes its real first rejecting branch and a
bounded CPU snapshot of context, slot, binding, recording and private/temporal
holders. `blocked`, `evaluated` and `unknown` remain distinct. Pending capacity,
outside-lock release in progress, quarantine and device-removed fence values
remain visible. A completed fence never cancels a live recording reference.
The original Ready/Invalidate calls and release permission are unchanged.

Normal execution with no retired candidates does not scan holder tables or
private/temporal owners. Stable retired detail refreshes at most once per second;
decision/state changes publish immediately. This CPU refresh interval grants no
release permission. There are no added GPU commands, readbacks or waits. F8
copies already-published data, using an 86,624-byte heap scratch instead of a
large callback-stack object. Core native lifetime/reset and retired-context
device-generation identities remain explicitly unknown until a reviewed seam
can supply them. Current `globalCodecDevice` is not proof of a retired device.

The metadata allocation remains 288 KiB. The end-only retirement projection is
capped at 24 KiB: all 32 contexts' core identity and masks precede optional
detail, with explicit omission/overflow counts. Detail capacity does not truncate
the known blocker masks or indirect recording-holder relationships. The full
bounded POD is retained for the active F8 session; omitted optional text is not
a claim that every holder was exported. Complete, source-written decimal
counter fields survive metadata sanitization, while pointers, paths, raw logs,
unknown fields and malformed schemas retain the privacy rules.

Windows controlled CPU checks passed 37 normal and four forced-detail-overflow
checks. Four compile-success runtime negatives catch missing pending masks,
extra Ready calls from F8, early overflow exits and lost indirect holders.
The actual formatter through the actual privacy policy passed 36 checks;
removing its typed-number exception fails the precise semantic-ID assertion.
The widest projection was 24,011 bytes. Controlled Clang O1 stack records were
1,464 bytes for the publisher, 376 for the formatter and 40 for the snapshot
copy; these are not measurements of the complete game's call stack or FPS.

Whole-Core Windows syntax and the existing combined F8 TXT/ZIP CPU fixture
passed (103 checks, plus its exact old-capacity negative). That isolated combined
fixture does not enable the new retirement writer, so it is not used as proof
of the new combined worst-width tail.

The complete assembled Core serializer was then compiled from accurately
exported commit `8787b9e8aa6c8be4b2635175f1bc0ccd2aa65068`, with verified
temporal/private module receipts. Seven fresh CPU processes passed: real clock,
unavailable clock and 12–16-digit deterministic clocks. The fixture supplies a
synthetic published retirement POD with 32 full-width typed contexts and full
detail tables; the actual Activity writer, final collector and disk output
preserve every context mask and semantic pending token, redact pointer fields,
and explicitly omit optional detail. Maximum metadata was 281,852/294,912 bytes;
the widest mandatory end tail was 179,049 bytes within the 185 KiB reserve.
The all-source text capacity check was 422,150/491,520 bytes. Removing the actual
end-retirement writer compiled successfully and made all seven processes fail
at the required `FULL_CORE_F8_RETIREMENT_END` assertion with exit41. These are
CPU serialization/capacity results, not native holder, NGX or game observations.

A first whole-Core syntax attempt exposed a too-narrow CPU-double owner type;
the helper now compares the production `const void*` identity without
dereferencing it, and the CPU fixture uses that actual type. The failed run is
preserved and not counted as validation.

## S2 event-pairing fix

Production commit `4275795e1486fffb02d17d9730ba35fde07d7fb4` pairs a true
`ID3DDestructionNotifier` callback with the existing NR recording ledger.
Marker IUnknown references remain independent of callback/ledger POD references.
The callback only publishes an atomic death flag and drops its own POD; it
does not dereference the dying object, acquire Core locks, touch registries,
collect/drain, log, call NGX or release models. The ledger captures native
identity and lifetime token separately from the recording token.

The existing collector consumes an exact dead notice through the original
unsubmitted-work quarantine and a pure ledger detach. It rechecks uncertainty
before owner maintenance or release decisions. Successful Reset still clears
the live object's old token before the same detach; failure preserves it.
Wrapper destruction only detaches the observation owner, retaining NR ownership
while the native list remains alive, even after marker replacement. Fence
completion, queue affinity, private/temporal/binding consumers, release-before-
create and outside-lock provider Release retain their original gates.

NR admission now requires a registered destruction notice from native Create
observation. Unsupported QI/registration or a late-wrapper-only list refuses
new NR before counter publication; native SR/RR state observation remains
available. Source inspection finds synchronous future-Create observation in
the normal pinned ReShade device-construction path. Actual game coverage,
other driver/Agility implementations and late injection remain unverified.
A safe-bypass FPS increase is not a performance improvement.

The accurate assembled source is `d72f011f3152872e1d76976e0baf2240cfd59a46`;
`31ed896fdd621fd2d6865cefcadea5770d601246` changes only the
controlled fixture's handle cast. Whole-Core WARP executions pass final
destruction, marker replacement, wrapper/raw-reference retention, marker-plus-
wrapper retention, successful and failed real Reset entry, pending own fence,
unsubmitted native death, target-only notifier QI failure and callback
registration failure. Native Release only publishes death; the normal collector
schedules the owner and the original outside-lock Drain calls an explicitly
controlled provider Release exactly once. No model or game is executed.

The two notification failure controls temporarily intercept only the target
native object's exact IID or the target notifier's callback registration, then
remove the hook. They do not inject death flags or clear NR counters. The
unsubmitted case retains the quarantined ledger and references with zero
provider Release. A separate actual-collector CPU fixture proves the second
uncertainty gate stops Maintain/Ready/slot invalidation and pending scheduling;
removing only that gate compiles and fails precisely with exit3.

The combined retirement CPU fixture now passes 41 normal checks (the original
37 plus four second-gate checks), four overflow checks and five precise runtime
negatives. Original parent/residency/release-admission/model-boundary and
submission controls still pass. Their native-event services are explicitly
controlled; the complete-Core WARP host supplies the native COM evidence.

The full queue host first failed to compile because its controlled handle used
`void*` rather than production `NgxHandle*`; the fixture type was corrected.
Its existing 27-scene output oracle then exceeded the offline 90-second process
deadline. The unchanged binary subsequently passed all 6,085 checks in 153.7
seconds with a bounded 180-second deadline. Only this offline fixture deadline
is extended; no production fence timeout or assertion changes. Both failed
attempts remain in the private evidence directory.

## Remaining verification

The complete 24-case runner, old and fix-removal exact negatives, and full
bilingual canonical entry are in progress. Canonical uses an isolated clean
`31ed896f` checkout with full face/FFX inputs; the initial PowerShell 5 host
misclassified successful unittest stderr as an exception, so that failed
attempt is retained and a fresh PowerShell 7 run is used. Actual game acceptance
remains pending. These checks cannot prove private-SR benefit, flicker repair,
0.4.7 performance parity or the holder path of old player feedback.
No game installation or player package is performed by this independent PR;
its internal main-based build must not be distributed as released Beta 13.

Execution specifications: [#339 runbook](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/339#issuecomment-6034176166),
[#309 dispatch](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/309#issuecomment-6034217346).
Review coordination: [#311 ownership seam](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/311#issuecomment-6035682050).
