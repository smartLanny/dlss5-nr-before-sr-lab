# Issue #330 memory observations combined with retirement evidence

This adapts PR #343 (`3e9659abb326ca779e5f88cb8ba1f8c1f1daa18a`)
onto public main `53f06b518839226fefd1d8db46a50dddb4b569c6`, including
the already integrated #341 retirement diagnostics and #346 network settings.
The original isolated validation remains in
`ISSUE330_BOUNDED_MEMORY_20261008.md`; its Windows results do not certify
this newer combination.

## Delivered behavior and integration fixes

The production memory slice retains its original scope: bounded F8 context and
slot allocation observations, sampled byte high-water marks, explicit unknown
groups, at most 32 cold model Create/Release observations, and a low-frequency
same-identity timing/process-memory trend. Quarantine is an overlapping ownership
attribute, not an additional total. DXGI deltas describe the complete process,
including concurrent noise; they are not model allocation, reclaimable memory,
memory savings, or FPS evidence. Native lifetime and fence decisions are unchanged.

Integration fixes are concrete:

- Preserve the 24 KiB retirement text projection in addition to #343's 337 KiB
  memory/activity tail. The combined mandatory reserve is **361 KiB**; the
  timeline adds its separate 4 KiB burst reserve.
- Generate the TXT `text.limitBytes` declaration from the actual `kTextLimit`.
  #343 had expanded the buffer to 832 KiB while still declaring 480 KiB.
- Expand the heap-owned ZIP manifest buffer from 384 KiB to a bound derived from
  metadata size: `6 * 640 KiB + 256 KiB = 4 MiB`. JSON can require six ASCII
  bytes per control byte. The extra envelope covers all 64 bounded artifact
  records and framing. Single-TXT F8 does not allocate this ZIP Job.
- Align the legacy ZIP parser with 640 KiB metadata and the 4 MiB manifest.
  Its existing 8 MiB physical/expanded archive limit and per-entry rejection
  still apply. Older smaller captures remain accepted.
- Preserve global/per-layer network serializer checks while moving expanded
  test sessions to the heap. Add the actual generic-algorithm helper to the
  picture serializer fixture, and add explicit missing-adapter services to
  the network Create fixture. Admitted Create observes two samples; refused
  Create performs no memory query or event publication.

## Cloud execution and evidence boundaries

`validate-memory-observation.py` now executes on Linux as well as on the pinned
Windows toolchain. Linux stages the production headers byte-for-byte and supplies
only controlled Win32/D3D12 service declarations. The actual CoreSlot/CoreContext
resource walk, allocation measurement dispatch, role accounting, timing window,
model-event formatter, privacy grammar, and trend function execute. No D3D device,
DXGI adapter, GPU, NGX runtime, or spatial GPU implementation is provided.

The gate passes two positive cases and eleven independently compiled mutations:
missing context, missing slot high-water, wrong resource role, mixed timing
identity, stale config/history, cross-node delta, failed usage marked valid,
missing timing written as zero, lost numeric privacy exception, removed trend
cadence, and reused interval timing. Each negative reaches its named assertion
with exit 41; compilation failure cannot count as rejection.

`validate-memory-f8-combined.py` executes these production pieces together:

- `FeedbackSession`, `FeedbackAppend`, `NrAppendAllocationLedger`, the allocation
  and model-event formatters, the published retirement snapshot copy, and
  `FeedbackAppendRetirement`;
- `SafeLines`, `Document`, and the complete `CollectSingleText` function;
- the real SHA256 implementation, JSON manifest encoder, CRC calculation, and
  ZIP local/central-directory framing;
- the actual ZIP parser, and `parse-core-text.py` when installed in the checkout.

Frozen owner samples and six bounded input summaries are fixtures. File access
and synchronization use explicit POSIX service adapters. One atomic append
observation hook detects an overlap without racing on the report bytes. Windows
atomic publication, Windows SDK ABI, real driver values and the complete Core TU
are not claims of this host fixture.

The fixture includes 32 contexts with eight occupied slots each, 32 model events,
all 32 retirement context masks, maximum-width counters, semantic IDs resembling
addresses, real pointer redaction, Chinese metadata, explicit unknown allocation
state, and all six maximum-size log summaries. Existing activity writers' 161 KiB
envelope and unused room in the memory writer's 176 KiB envelope are filled with
clearly named fixture allowance rows. This verifies that the independent producer
budgets remain composable; the Windows full-TU test separately executes the
remaining activity writers.

| Host combination measurement | Observed / bound |
|---|---:|
| Metadata | 654,530 / 655,360 bytes |
| Actual end allocation/event output | 155,199 / 180,224 bytes |
| Actual retirement projection | 23,928 / 24,576 bytes |
| Combined mandatory tail, including explicit allowance rows | 368,835 / 369,664 bytes |
| TXT, including six 24 KiB summaries | 801,455 / 851,968 bytes |
| Worst JSON escape stress, including 64 maximum-width records | 4,143,549 / 4,194,304 bytes |
| Allocation Snapshot | 3,632 bytes |
| ContextDetails storage, heap only | 60,160 bytes |

The actual allocation and retirement append functions also complete on a pthread
with an explicitly configured **65,536-byte stack** while another thread holds
the feedback lock. Sampling occurs before waiting for that lock; append does not
overlap the observer. The GCC `-O1 -fstack-usage` receipts report 14,608 bytes for
the allocation append function, 320 bytes for retirement append, and 1,872 bytes
for the TXT collector in this controlled fixture. These are host compiler frames,
not a Windows/game stack measurement.

Seven independent compiled controls reject removing the append lock, losing the
24 KiB retirement reserve, restoring 288 KiB metadata, restoring 480 KiB TXT,
restoring the 384 KiB manifest, omitting retirement output, and emitting the stale
TXT limit declaration. Each exits 41 at its exact named assertion.

ASan and UBSan pass the positive combination, including the 64 KiB thread and
maximum JSON expansion. LeakSanitizer could not read the sandbox `/proc` task
inventory and aborted; the successful sanitizer run explicitly used
`ASAN_OPTIONS=detect_leaks=0`. It is not a leak-check result.

Additional regressions: all 34 ZIP parser cases, 36 retirement privacy checks,
the existing 41-check retirement lifecycle fixture and binding-overflow controls,
and the picture serializer's 33 checks pass. The portable suite initially found
one missing memory service in the network Create test; the fixture was repaired
and its complete production network/caller/admission/retirement gate passed on
rerun. Across the suite, **65 test files pass and three prerequisites remain
explicit skips**: the two Windows UI fixtures and the historical replay capture.

## Reproduction

Use fresh directories; existing evidence is not overwritten:

```bash
python3 scripts/validate-memory-observation.py --compiler g++ --output build/memory-observation-evidence
python3 scripts/validate-memory-f8-combined.py --compiler g++ --output build/memory-f8-evidence
ASAN_OPTIONS=detect_leaks=0 python3 scripts/validate-memory-f8-combined.py --compiler g++ --output build/memory-f8-asan --positive-only --sanitizers
python3 scripts/run-portable-tests.py --verbose
```

The generated `validation.json` records source/function hashes, compiler commands,
expected assertions and exits, capacities, and parser results. The combined runner
accepts `--source-root` and `--text-parser` for independent integration worktrees.
The new portable tests invoke both gates automatically.

On Windows, retain the actual SDK/COM and full-Core gates:

```powershell
python scripts/validate-memory-observation.py --compiler <clang++.exe> --source-root <assembled-Core> --output <fresh-output>
python scripts/validate-allocation-ledger.py --compiler <clang++.exe> --output <fresh-output>
python scripts/validate-feedback-timing-full.py --compiler <clang++.exe> --reshade <ReShade-root> --source-root <assembled-Core> --source-manifest <source-manifest.json> --output-dir <fresh-output>
```

Run the full-TU negative controls `old-tail-reserve`, `old-metadata-limit`,
`old-text-limit`, `omit-end-retirement`, and the existing typed-privacy/identity
controls against that exact assembled source. The Windows allocation gate retains
its WARP resource measurement, 64 KiB worker test and lock-removal control.

This batch does not build or publish a new binary, change the Manager pin, prove
native GPU retirement, prove memory reclamation, or certify an NGX/game route.
The source integration and cloud diagnostics are ready for the canonical Windows
gate and later game acceptance; those results must be recorded independently.
