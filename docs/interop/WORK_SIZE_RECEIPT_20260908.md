# #82 coherent native-post work-size receipts

This isolated diagnostic continuation depends on draft #183 at
`81e56e3a3737ab254171aad22ea3dcac0457eac0`. Production, generated CPU fixture and
complete DLL source are `ed38ae9fb289cec006f25ac7b540f5a63f558c96`.
It changes no NR algorithm, guide conversion, dimensions, shader, Manager or
0.5 experiment. No GPU matrix or game run was performed.

## Exact supported scope

The new receipt supports **native SR/RR post-NR only**, after a successful
Feature 18 evaluation and actual composition recording. `FinishAfterSr`
already holds one accepted invocation's authoritative values:

- R: `substitution.guide_width/height`, resolved from that native invocation.
- N: `context.width/height`, the actual NR context/model dimensions.
- O: `substitution.width/height`, the resolved full native output extent.

The publisher runs only after `RecordVisibleTransferLocked` succeeds. Warmup,
identity retention, failed preparation/evaluation or declined transfer does
not create a receipt. Feature 1 and 13 select separate native-post-SR and
native-post-RR route labels. The data is copied under the same chain lock,
without assembling independent last-width/height atomics.

The identity is the existing real NR evaluation token from
`CompleteNrDiagnosticEvaluation`, with feature generation, config/history
generations, NR instance and slot-use serial. It is **not** an engine frame,
view or presented/FG frame. Game frame/view and an independent allocation
generation remain explicitly unknown; NR instance and slot-use serial are
labelled as their actual lifetime/use identities, not renamed into a fabricated
view or resource-generation id.

Pre-SR is not supported by this first diagnostic slice: its Reference early
return does not resolve O in the work planner. External-provider ABI V1 only
contains its own R-to-R NR boundary, not the eventual downstream SR O. Neither
path is filled from separate game-size atomics or mislabeled as a complete
R/N/O receipt. A later diagnostic-only extension can resolve authoritative O
at those boundaries without changing their processing policy.

## Recording, submission and completion are separate facts

One fixed-size `nr_work_receipt::Receipt` stores the latest supported recording.
It has no owned resource or queue pointers, unbounded collection or GPU memory.
A newer supported recording replaces the tuple and resets submission/completion
facts. Historical receipts can outlive their configuration or context; they are
always displayed as **last recorded**, not as the current request or frame.

The receipt copies the existing `context.pending_recording_token`. Only
`CompleteSubmissionBatchLocked`, reached after native Execute and successful
retirement Signal, may bind that exact matching command token to a real queue
identity and fence value. Merely reserving a fence at Capture is insufficient.
Quarantine/lifecycle uncertainty prevents publication of new submission proof.
An unrelated or late old submission cannot update the latest receipt.

Snapshotting may nonblockingly call the existing queue fence's
`GetCompletedValue` for the matching queue identity. Completion is marked
observed only for that submitted value; zero/missing proof, another queue,
pending values, UINT64_MAX/device removal and lifecycle uncertainty do not
produce completion evidence. No event, GPU wait, new signal, command submission
or readback is added. The recorded/submitted timestamps and first completion
observation timestamp use GetTickCount64 milliseconds; the latter is an
observation time, not a GPU timestamp or measured duration.

## F8 / UI behavior

The core F8 collector copies the receipt inside its existing TryEnter chain-lock
snapshot alongside configuration and applied identity. Its text is frozen
before the worker runs. Later records cannot change that captured tuple. Lock
contention yields `snapshot-unavailable`; absent supported recording yields
`unknown-no-supported-recording` with no fake zero-sized valid tuple.

`workReceipt.*` fields state coverage and historical meaning, game frame/view
unknown, actual identity types, route, R/N/O and recording time. Submission and
completion carry explicit observed flags. Fence identifiers/timestamps are
printed only when actually observed; otherwise the fact is `not-observed`.
The existing feedback format and bounded collector remain in place.

The developer panel shows the same coherent last-recorded tuple, historical
route, record time, observed/unknown submission/completion and separately labelled
current versus recorded config generation. The independent sampled game R/O
display remains a separate observation. It does not infer N or imply that
an old native-post receipt describes a currently active pre/external route.
No new game view id or artificial frame counter is introduced.

## Actual local verification and complete links

The generated stage is private `build/work-receipt-generated`. The test extracts
the actual publisher and snapshot helpers from that generated translation unit
and compiles them against the production receipt policy. All **26 CPU cases**
passed: coherent values/identities, unsupported external route, no recording,
wrong instance/config, missing dimensions/token, recorded-versus-submitted,
old and mismatched command/queue/fence, device removal, uncertainty, new-record
reset, exact completion and frozen-copy independence. Source-order checks verify
the real post publisher follows evaluation/warmup/transfer gates, submission
publication follows Signal's success gate, and F8 copies within its existing lock.
These are CPU/mock-fence contract tests, not GPU completion or model tests.

```powershell
python scripts/validate-work-receipt.py --compiler $Compiler `
  --source-root build/work-receipt-generated/core
```

The first test-harness compilation lacked `<initializer_list>`; its original
failure log is preserved. After that harness correction, the final frozen
generated-source tests passed without GPU execution.

The unmodified generated `core/scripts/build.ps1` then completed bilingual Core
and nrchain linking from ed38ae9. All **117 original manifest entries** still
match after building. Both Core binaries embed the exact source commit and
numeric PE version 0.4.6.1. The inherited hotfix.1 label does not identify an
old release asset; hashes below identify this private diagnostic build.

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| English Core | 1,352,704 | `b5232b6b121afb3ab2d07e93150d189e3fe6f514215b382d72c644d17a1e9415` |
| Chinese Core | 1,375,744 | `17e8c1d24ba3ada9ed5e71c94439925849c77d696be8da155143d482a0eb153e` |
| nrchain | 8,192 | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

The unchanged nrchain shim contains no Core source/version resource.
Generated Core SHA256:
`5f239b1934b0493abea87a9a6eb278c16509256279e4514b65287e2763d71593`.
Manifest SHA256:
`7ce5d029359502e9ddd2271f41bb620d787f5f50fa1d7343c0f45768606d2ebc`.
Private `build/work-receipt-validation/validation.json` SHA256:
`7cdf828fdd4bb4312163e5f313d041ffeafdbdd43b875d0aa80a1cb60b83507e`.
CPU log SHA256:
`afdf15168052f70e8d7b3e1413f79d4e2f4dbe656f332d39bc376ef611bdd400`.
Full build log SHA256:
`5699a9c593c7b06bda59e1a255afc58bbb37066acd0b3821fa7d47e7da0d8cfd`.
All warnings remain in the full log. No binary/private raw evidence is tracked.
The coordinating task independently reviewed the production publication point,
Signal gate, nonblocking fence observation and frozen F8 copy, including actual
RR Feature 13 propagation and R from the resolved Color active rectangle.
It independently rehashed all 117 manifest entries, all three artifacts and
validation 7cdf828f; every value matched.

Remaining #82 work includes pre/external coherent receipts, engine frame/view
and independent resource generation if a real provider supplies them, live UI/F8
and real queue behavior, 1–49% admission, game/DPI/stability and performance.
This build does not complete those acceptance items and has not been merged,
released or run on GPU. After independent review, the coordinating task
authorized this batch to extend existing draft #183, preserving the separate
ed38ae9 DLL identity and previous 3411 configuration build identity. Publication
of source is not additional execution evidence.
