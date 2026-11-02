# Feature 18 network selection and runtime identity

Prepared on 2026-10-18 while the Windows development host was unavailable. The
implementation builds on the existing exact queue/fence retirement ownership;
it does not add a DLL unload/reload path or change native SR/RR ownership.

## Source receipts

The interface was checked against the latest project issue notes and actual
upstream source, rather than inferred from a model filename or a UI label.

| Receipt | What it establishes |
| --- | --- |
| [Project #309, latest update](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/309#issuecomment-6233027890) | Current integration scope, model identity evidence, and the limits of upstream/game validation. |
| [Project #274, latest update](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/274#issuecomment-6233033039) | Independent layer network requests, Create/recreate semantics, and separation from Style and legacy presets. |
| [OpenShaders #956](https://github.com/alandtse/open-shaders/pull/956), head `9bd81177956428fff5beaef8ecc170bb794fae29`, merge `4f21680c0f3e12f0be0c6846c865b687df326e97` | `src/Features/Upscaling/NeuralRendering/Runtime.cpp` sends **UInt `DLSSNR.Hint.Render.Preset` at Feature 18 Create**. Its zero-based UI choices map to native 1/2; those UI values must not be copied directly. |
| [OptiScaler multipass #128](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/pull/128), head `45437a2f8336794ecd4f1817b26fcf0d3e0a2634` | Independent per-pass selection; explicit runtime default remains different from inheritance. The reviewed PR was open and compile-checked, not a game compatibility certificate. |

The reviewed upstream file receipts are:

| SHA256 of the DLL file | Recorded family |
| --- | --- |
| `2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f` | 310.9.16, Original/HT and Optimized/CC_SMALL_FP8 |
| `8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206` | Original-only 310.8 family |
| `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e` | Original-only 310.8 family |

These receipts describe published files. Neither a version resource nor a file
hash proves which internal network a particular in-memory execution used.
An unknown compatibility build remains unknown, including builds that retain
one of the above version strings.

## Offline candidate inspection

An explicitly supplied candidate can be inspected before replacing any runtime:

```sh
python scripts/inspect-dlsnr-candidate.py "C:\Candidates\nvngx_dlssnr.dll" --output candidate-inspection.json
```

The script uses only the Python standard library and opens that one file for
reading. It streams SHA256 in 1 MiB chunks, reads bounded PE32/PE32+ headers,
reports architecture and section/resource-directory summaries, and interprets
only numeric RT_VERSION fixed file/product version fields. It does not load or
execute the DLL, find or replace installed runtimes, download files, extract
weights, or search arbitrary payload strings. The optional JSON output must be
a new file; an existing report or the candidate itself cannot be overwritten.
Without `--output`, the report is written only to standard output.

The file limit is **1 GiB**, matching the production identity worker. Metadata
reads have an 8 MiB total budget and a 64 KiB individual-read limit. The parser
bounds section count to 96, resource directories to 2,048, total resource entries
to 8,192, directory depth to eight, resource names to 256 UTF-16 code units,
resource types to 256 and version records to 32. It rejects overlapping section
mappings, unbacked/overflowing RVAs, invalid relative offsets, repeated/cyclic
directories, and invalid or oversized fixed-version records. Unusual files
outside these limits are reported as rejected metadata; that is a limitation of
this inspector, not a complete Windows loader verdict. Stat identity changes
during reading invalidate the combined hash/metadata receipt.

The three hash receipts remain synchronized with `nr_network_policy.h` by a
test. A known digest identifies only the reviewed **disk file**; an unknown
digest remains unknown even when its fixed version is familiar or resource
names contain `CC_SMALL_FP8`. Disagreeing fixed-version resources and a known
digest contradicting successfully parsed architecture/version metadata are
reported as conflicts. A parser-limit failure preserves a stable hash receipt
without inventing a metadata conflict. Authenticode, the loaded module, NGX and
GPU execution are explicitly **not checked**, and network execution is never
reported as verified.

Exit code 0 means the bounded inspection completed without contradictory
metadata; it does not approve model availability or installation. Rejected
metadata/file inputs or conflicts return 1; CLI/report-output errors return 2.
The PE layout references are Microsoft's [PE format specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
and [VS_VERSIONINFO definition](https://learn.microsoft.com/en-us/windows/win32/menurc/vs-versioninfo).
`python tests/test_dlsnr_candidate_inspector.py -v` executes seven synthetic
fixture tests, including the real CLI, malformed metadata, budgets, file changes
and protection against overwriting the input. No vendor DLL was supplied to or
validated by those fixtures.

## Configuration and native values

| Saved value | Meaning | Value sent to Feature 18 |
| --- | --- | --- |
| Global `NRNetworkPreset=0` | Let the loaded runtime select its default | UInt 0 |
| Global `NRNetworkPreset=1` | Request Original/HT | UInt 1 |
| Global `NRNetworkPreset=2` | Request Optimized/Small FP8 | UInt 2 |
| `Layer1NetworkPreset=-1` through `Layer5NetworkPreset=-1` | Inherit the global request independently for that layer | The resolved global UInt, never -1 |
| Any layer override 0 | Explicitly use the runtime default, even when the global request changes | UInt 0 |
| Any layer override 1 or 2 | Select that network independently | UInt 1 or 2 |

The initial global value remains 0, and every missing layer override remains
-1. Updating the DLL can therefore change the runtime-selected network: the
reviewed old family defaults to Original, while the reviewed new family defaults
to Optimized. Choose explicit 1 when an Original request is required.

Example: request Optimized in the first two layers and Original in the third:

```ini
NRNetworkPreset=2
Layer1NetworkPreset=-1
Layer2NetworkPreset=-1
Layer3NetworkPreset=1
Layer4NetworkPreset=-1
Layer5NetworkPreset=-1
```

Layer enablement is independent of the saved network values. These settings do
not enable additional layers. Unknown numeric IDs remain saved verbatim and are
rejected if requested by an active layer; malformed/overflowing integers are
also rejected. An inactive saved override does not block valid active layers.
The obsolete `NRPreset` key is not reused.

## Creation, cache identity, and refusal

`NrTuningSignature` now includes the resolved native network request, loaded
module generation, and loaded module address. A changed native request retires
the affected Feature 18 context through the existing exact retirement path.
An equivalent change from inheritance to an explicit value does not recreate
the model. A global edit does not rebuild a layer with an unchanged explicit
request. Existing parent-history Reset propagation continues to apply.

`CreateSignedNrFeature` validates its saved request and runtime identity before
marking private work recorded. It writes the typed network key to the owned NR
parameter block before calling the signed bridge. The Style, intensity, skin,
tone, structure, jitter and native SR/RR parameter contracts are unchanged by
network selection. Failed Create calls returning a non-null handle retain the
existing process-lifetime dependency quarantine.

The whole active chain is admitted at its primary context lookup. An auxiliary
lookup cannot repeat that check and retire the primary while the current chain
is already being prepared. Every Create still checks its own current request
and loaded identity before entering NGX. The Create boundary reports whether
the opaque API was actually called, separately from the numeric result code.
This distinction matters when the existing worker publishes a known-old file
receipt between primary admission and Create: native pre/post, auxiliary,
private FG and Present consumers retain the network refusal and avoid inventing
an NGX error, failure counter, retry delay or permanent quarantine.

A nonfatal auxiliary refusal propagates through both the two-layer adapter and
the complete three-to-five-layer wrapper. Native pre/post consumers retain the
original image for that source rather than adopting a partial chain and clearing
the refusal. This does not alter ordinary model warmup or resource preparation
fallback. Timing ends as unadopted. If replay has already bound the immutable
composite view to the private result, its existing `Attempt` ends incomplete;
it never reads that unwritten result as a supposedly completed native replay.
The recorded capture resources stay under the existing retirement ledger.

Earlier legal GPU commands remain owned by their existing recording. Present
submits its initialization or successful earlier-layer packet without adoption
and obtains an actual fence, even when the next Create is declined. It neither
declares a Create fence nor discards the resource-state ledger. The next primary
boundary rejects the unavailable chain and retires its dependencies normally.
Real NGX failures, failed Signal after Execute and ambiguous non-null handles
retain their original opaque-failure protection. Repeated network refusals also
avoid the generic per-frame "No free NR context" message.

A verified known Original-only **file receipt** refuses Optimized instead of
silently treating an old-runtime fallback as success. A version-only hint does
not refuse the request. An unrecognized DLL is allowed to try its saved native
request and remains explicitly unverified. A newly learned file hash does not
change the context creation generation by itself.

Present records the actual layer index. Its first initialization refreshes the
tuning signatures after `EnsureNrApi` publishes the loaded runtime generation;
it cannot retain a pre-load generation-zero signature and recreate it on the
following frame.

## Runtime and F8 evidence

`RecordNrNetworkRuntimeLoaded` samples the bounded version resource of the
already loaded `HMODULE` once. The normal Create/Evaluate path performs no DLL
file hash or directory scan. Immutable atomically published snapshots let the
UI read identity/status without a new lock or disk access.

The existing pinned installation-status/F8 worker measures the file at the
actual loaded module path, once per module generation. The worker reuses the
production SHA256 implementation, reads in 64 KiB chunks, bounds the file size,
and caches both success and failure. It does not load a candidate DLL. An early
worker before NR initialization does not consume the future measurement.

F8 records the loaded path, file SHA256, mapped version hint, evidence level,
generation and status. A known file hash contradicting mapped version metadata
is reported as a conflict, not as model availability. An unknown nonempty hash
supersedes an otherwise familiar version hint. Diagnostics explicitly label the
hash as a **disk file at the loaded module path**, not relocated memory,
Authenticode, or an executed-network receipt.

Requested configuration and historical model observations remain distinct:

- TXT and frame JSON contain the global request and all five raw/resolved layer
  requests, including unknown saved values.
- Applied tuning, stage metadata, frame JSON and the F8 timeline add the
  creation-time network request and runtime generation/module identity.
- A bounded native-context snapshot reports layer, instance, created handle,
  evaluation count and retirement state. Present creation logs carry the same
  request/identity fields.
- Full model replay records UInt `DLSSNR.Hint.Render.Preset` from the owned
  parameter block. The scalar table uses 61 of its existing 64 slots; the 6080
  byte Header ABI and reader version remain unchanged.
- `networkExecutionVerified` remains false. Successful Create/Evaluate calls and
  parameter readback do not provide an internal network-execution API.

Replacing the DLL requires restarting the game. Network changes within the same
loaded DLL use retirement and recreation; they do not invoke NGX Init/Shutdown
again.

## Validation completed without Windows

Run the portable gate from the integrated source tree:

```sh
python scripts/validate-nr-network.py --output /tmp/nr-network-receipt
python scripts/run-portable-tests.py -k test_nr_network_selection --verbose
python scripts/validate-050-stability.py
python scripts/validate-050-stability.py --negative-control
python scripts/validate-guide-history.py --compiler g++
python tests/test_nr_dual_observation.py --compiler g++
```

The network gate records source and fixture hashes and compiles the actual
production function bodies. Completed Linux CPU checks cover:

- 125 policy/typed-binding/version-resource bounds and evidence checks.
- 33 production metadata/identity-worker/cache/admission checks with Win32 IO
  observation stubs and the production SHA256 implementation.
- 42 production Create-boundary checks with an observed NGX bridge seam,
  including unknown/invalid requests, missing-network refusal, stale module
  identity, and failed-Create handle quarantine.
- 130 checks through actual caller Create branches, native final-consumer
  decisions, replay Attempt/scalar capture, and production Present
  Submit/Timeline/RetireProof with controlled fake fence completion. They cover
  a known-old file receipt arriving after initial admission, native pre/post,
  auxiliary, private FG, Present primary/later layers, correct failure reasons,
  real opaque failure protection, and unavailable whole-chain output retention.
- 125 complete production full-adapter checks, including refusal at every
  auxiliary position in two through five layers, ordinary preparation/warmup,
  opaque failure, and final adoption. An equivalent explicit `FullSnapshot{}`
  assignment allows GCC to compile its nested aggregate initialization.
- An independent production Python reader decodes the actual C++ replay Header
  and confirms that the network scalar remains present, unsigned and unchanged.
- 16 compiled negative controls remove real Create binding/admission guards,
  skip required Present fences, hide full-chain refusal, finish an unwritten
  replay composite, omit the replay network scalar, or reintroduce nested
  auxiliary readmission. Each fails at the corresponding executed boundary.
- Actual context selection across five independent network requests, global
  inheritance and explicit default, equivalent requests, changed module
  identity, A-B-A retirement, and the existing 128-history-epoch reuse scenario.
- Actual TXT/JSON global/per-layer request serialization, unknown saved IDs,
  bounded report behavior, the existing serializer negative control, guide
  contracts, and NR2 face/model observation continuity.

These are CPU contract tests. They do not establish Windows ABI/build success,
NGX acceptance on a real NVIDIA device, GPU retirement completion, image
quality, frame timing, or compatibility with any game.

### Complete portable suite on the integrated #341 base

One complete run of the unmodified `scripts/run-portable-tests.py --verbose`
entry point was recorded on Linux with Python 3.12.14 and GCC 13.3.0, based on
`f9ef953faad0a31d280d27adbbe411433a352a47` (#341) plus the network runtime and
settings/UI patches. An external observation wrapper preserved each child
process's full stdout/stderr. Source SHA256 manifests before and after that run
were identical. **This run preceded the later Create-admission race correction
and version-label edits.** Their new targeted regression receipts are recorded
separately; this full-suite result must not be relabeled as a run of those later
sources.

The runner printed **49 PASS / 8 FAIL / 3 SKIP** and returned 1. Seven of its 49
exit-zero files contained only skipped unittest cases, so **42 files had actual
checks and completed successfully**. There were **38 internal unittest skips**
in addition to the three top-level skipped files. All were retained separately;
this was not a full-suite pass. New network settings, the production UI recorder,
NR2 observation, picture reset and UI refresh checks passed; the unified UI
checks retained their one existing internal skip.

| Failed file | Observed cause and independent check |
| --- | --- |
| `tests/test_archive_index.py` | The checkout is shallow; complete history/tags are unavailable. No ancestry verdict was made. |
| `tests/test_lockperf2_workflow.py` | Required historical Git objects for the old workflow are unavailable. |
| `tests/test_mainline_maintenance.py` | Required historical instruction/workflow Git objects are unavailable. |
| `tests/multi_api/test_rr_post_diagnostics.py` | GCC string-truncation diagnostics in `nr_processing_runtime.inl`; reproduced on clean #341. |
| `tests/test_compat_route.py` | GCC misleading-indentation diagnostics in descriptor/source-frame code; reproduced on clean #341. |
| `tests/test_menu_trace.py` | GCC enum/integer conditional diagnostics and `Snapshot` assignment error; reproduced on clean #341. |
| `tests/test_retirement_evidence.py` | GCC misleading-indentation and nontrivial-type `memset` diagnostics; reproduced on clean #341. |
| `tests/test_retirement_privacy.py` | GCC misleading-indentation diagnostic in `nr_feedback_text.h`; reproduced on clean #341. |

Only the five compilation failures were rerun in a separate, clean worktree at
the exact #341 commit with the same Python/compiler. All five failed there too;
their compiler error messages matched the integrated failures exactly (4, 3,
31, 9 and 1 messages respectively). The baseline worktree remained clean. No
production guards, test gates or `-Werror` flags were changed to produce a pass.
The three top-level skips require Windows or a missing replay capture produced
by the Windows harness.

The retained local `network-portable-validation.zip` receipt contains the full
runner output, all child outputs, compiler/source manifests, nested skip counts
and the five baseline reruns with exact error comparison. Its SHA256 is
`bce3f4dee04a6364511d5dc7adb7bcba5f0ea4c9b4d881d6616f34bf3091e65c`.
It contains CPU/source evidence and does not replace Windows/NGX/GPU retirement
or game acceptance.

## Windows work prepared

### Final candidate identity and CPU rerun

After the reviewed refusal/consumer/replay corrections, the candidate source
identity was advanced to **0.5.2-beta.17**. The C++ display/build identity,
Chinese/English addon names, PE resource versions, build output names and final
packaging metadata agree. The existing PowerShell BOM/CRLF formatting was
retained. No binary was created or relabeled.

The final standalone network validator was rerun on this exact C++ SHA256:
`f8ea555c7ac704f3aa4926e4fe001155ca66bafb6b5e4f2f78dd56a8e869f38d`.
It passed the 125 policy, 33 worker/runtime, 42 Create, 130 actual
caller/consumer/replay and 125 full-adapter checks, the independent replay
reader, all 16 compiled negative controls, context identity and 128-epoch
reuse cases. The final configuration/inspector/UI and two product identity
tests were also rerun successfully. See the normalized, path-free
[final CPU receipt](NR_NETWORK_CPU_20261018.json); the earlier full portable
suite retains its original failures/skips and earlier source boundary.

The only existing full-adapter portability adjustment was the equivalent
explicit aggregate assignment `out=FullSnapshot{}` in the already modified
`nr_full_runtime.inl`. No compiler warning or production guard was disabled
to pass this network gate.

The separate optional colour source review is [PR #345](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/345).
It is not a dependency of this network PR. The concise Chinese
[Windows handoff](NR_CLOUD_HANDOFF_20261018.zh-CN.md) records integration order,
the existing build entry, the four network arms and the limited next game check.

### Existing hardware host

The existing real NVIDIA Present host now accepts one additional scenario
option:

```text
compat-present-ngx.exe <output-directory> 3 100 --network=2
compat-present-ngx.exe <output-directory> 3 100 --networks=2,2,1
```

The existing runner exposes corresponding `--network 0|1|2` and
`--networks 2,2,1` arguments. `--network` applies to its usual case matrix;
`--networks` chooses one 100% work-scale case with the specified layer count.
Use the runner's existing frozen-Core, compiler, ReShade, runtime and
bootstrap prerequisites. `--compile-only` remains available. The runner rejects
these new scenarios for a frozen Core that lacks production network support.
The historical `--preset-zero` control remains available for old frozen Cores.

The host reads back the actual creation parameter, checks runtime/context
identity and retains its existing exact-fence/output capture checks. Its inputs
are artificial; a passing host run would still not prove face/hair/eyes quality,
game output correctness, or an internal executed-network identity.

The Windows host additions are prepared and Python syntax-checked, but were not
compiled or run on Windows in this session. The required next hardware checks
are the existing full Windows build/ABI/retirement gates, network 0/1/2 and
mixed-layer host cases, then matched game captures and model-only timings with
the same resolution, settings and warmed-up history. No model quality or
performance threshold has been fabricated from upstream anecdotes.
