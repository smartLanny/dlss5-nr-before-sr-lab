# D21 Perf7 Windows validation and owner test delivery

## Frozen identity

Actual compiled source: `9184fb60c0e98191069f091171ff4dfccf52440d`.
Runtime implementation commit: `d189463a1d9bff225f69e90dad6249550998e558`.
The intervening commit updates the explicit generated-input contract for the new
source-state table; it does not change runtime logic.
Source-transport validation follow-up: `5b04faae971e5c38ff57c2fb61550389b8dee74b`.
This follow-up changes only the test extractor to include the ACTUAL new state
table, not the compiled Core. Later OTA receipt-name correction and delivery
notes also do not change the frozen production inputs.

Version string: `beta0.5-dline21-perf7`. Numeric PE version is inherited 0.5.1.23;
identify the actual candidate using string version, source commit and SHA256.

| Artifact | SHA256 |
|---|---|
| Chinese Core | `b72b5c3859fd288165414e5644e3d065edbba74102e354628aed517446d25d5d` |
| English Core | `6c0691f9b8fac45bf884aab994c12a908d41ee58f233964ec5a767875033a6ce` |
| Matching nrchain (unchanged from Perf6) | `1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2` |
| Chinese Core-only ZIP | `58e61bc97bb5a9e085b31f5a38a69f42bb3eeccb922ce3ffd44eddcccbbd1978` |
| English Core-only ZIP | `40afa19cde2b033385d76040134ea17b83d50cc0ae4f92595382ed04dceb2341` |
| Chinese Core + nrchain OTA | `a4e35473b6825673b88fa5e66edfb2ed231dcd5522507df29d1e14b91fb0854a` |

Recommended package: `DLSS5-0.5-D21-Perf7-zh-CN-OTA.zip`, 1,649,600 bytes.
It contains one Chinese Core, matching nrchain, licenses, instructions and an
`nr-branch-ota-v1` manifest. No INI, model, NVIDIA game runtime or installation
change is included. ZIP CRC, file set, byte lengths, hashes, x64 PE, embedded
version/source and companion exports were checked. Manager UI import is not
runtime-verified. The owner handoff supplies the actual machine path; raw local
paths/captures are not included in this source document.

## Gates actually executed

- Production-body Perf6/Perf7 comparison: 300 capacity-fill operations and 3,500
  mixed barrier batches. Covers capacity, aliasing, UAV, enhanced invalidation,
  split/partial-subresource state, null/disabled paths and lifetime retirement.
- Hot state update: 65,152 comparisons -> 1,024 probes; within-call identity
  queries 256 -> 4. Pure-UAV calls retain the original lock and identity
  publication side effect. No native barrier is skipped.
- Interleaved-list lookups: 32,712,000 native snapshot full-scan comparisons -> 0;
  4,040,000 observed-list scans -> 0; device comparisons 72,000 -> 16,064. Hits
  still validate the full recording/lifetime contract.
- Real Windows SRW: 40,000 concurrent snapshots across 4,998 writer generation
  changes. Alias expiry/rebinding, readiness, Reset and address reuse compare
  identically with the frozen baseline. Warm device COM queries 1,000 -> 0.
- All prior SRW/Lock2/Perf3/4/5/6 gates retained and passed.
- Source ownership policy: 399 checks. Existing compatibility route tests run
  with an explicit compiler, including 36 production-suffix cases and negative
  controls, not silently skipped.
- Multi-API suite: 76 tests, 71 passed and 5 skipped in the separate manual run.
  The complete build's own source/compiler gates are recorded in validation.json.
- Full staged Core/canonical pipeline: 46 validation groups passed, including
  bilingual compile/link and LLVM static analysis with zero diagnostics.
- Exact generated tree: 229 file hashes verified; 164 pinned Core inputs; four
  changed production files equal the frozen Git bytes and staged compile inputs.
- Descriptor CPU: 2,225 checks. Whole generated Core descriptor WARP: 669 checks.
- Source-transport WARP after extractor correction: 1,700 checks. The receipt
  records BOTH runtime and new state-table SHA256; both match the compiled input.
- Native state restoration WARP and wrong-state negative controls retained.
- Post-SR snapshot WARP: 1,045 checks / 3 scenarios plus wrong-pixel negative.
- 26 final shader files were rehashed and are byte-identical to Perf6.

The microbenchmarks include the actual observer body with identity doubles, not
real game/driver timing. A tiny ideal first-slot/single-barrier case has a small
absolute overhead (~0.02 ms across 2,000 calls); no claim that every microcase
is faster. The larger hot/full-state cases improve, but no game FPS gain is
asserted until the owner retests.

## Failure history and exact-source completion

The first build stopped at the dependency allowlist, which was updated explicitly.
A subsequent attempt encountered a full work drive: the 32 MiB feedback test
fixture was truncated before completion. Only this task's generated failed-build
files were relocated; no original captures, old candidates or other worktrees
were deleted. The isolated worktree's ignored test-output directory points to
space on the build drive. The actual feedback test then passed all 347 checks,
and the complete build ran on the drive with available space.

The full bilingual Core and canonical WARP pipeline completed for frozen 9184fb60.
The final source-transport wrapper initially stopped because its extractor still
looked for the removed `struct ResourceState` declaration. Commit 5b04faae updates
that test-only boundary to include the new production table. The 1,700 real WARP
checks then ran successfully against unchanged runtime bytes.

Rather than relabeling another DLL or skipping a gate, finalization verified all
canonical/inherited/Perf7 receipts, the full source manifest, the exact runtime
and state-table hashes, and the snapshot readback/negative evidence. It then ran
the byte-identical packaging suffix from the frozen builder. `PERF7-RESUME.json`
records the validation follow-up, source identity, packaging suffix hash and
unchanged production inputs. The earlier process status/log remains `failed` as
historical evidence; it was NOT edited into a fictitious clean single-run pass.
`PERF7-CANDIDATE.json` and the OTA receipt describe the resulting completed gates.

## Explicit limits

This host lacks the D3D12 debug layer. The explicit AllowMissingDebugLayer option
ran real WARP readback/negative controls but records
`snapshot_debug_layer_verified=false`. No Windows component, driver or security
policy was changed. Inherited HLSL/novtable warnings remain in build logs; zero
LLVM analyzer diagnostics does not mean an entirely warning-free build log.

No new-candidate NVIDIA Feature18 inference, game FPS, weak-CPU throughput,
long-running game stability or Manager UI deployment has been verified. Hosted
CI must be read from the actual PR check results; local validation does not mark
hosted CI passed. Cold initialization stalls have not been moved to arbitrary
background work or hidden by OFF-state prewarming.

## Next owner test

The last Perf6 recording used AFTER-SR, 4K NR and intensity/local controls 2/2/2.
Retain those settings for a direct Perf7 comparison: OFF 15 s -> ON 15 s -> OFF
15 s, then capture F8. A BEFORE-SR run must use a matching BEFORE-SR baseline.
Check the real game PID and growing NR successes; do not use the stale manifest
PID or manual folder name as proof of state.

The nearly full work drive is not suitable for the next multi-gigabyte ETL.
Beside the OTA there is a Record-Perf7.cmd/ps1 convenience entry that calls the
unchanged recorder with its existing Root parameter to a drive with space.
It was syntax-checked only and NOT executed; it neither installs the Core nor
changes settings or presses F6. Original recordings remain untouched.

Keep one Core and its matching nrchain, back up old files outside the game,
retain INI/models/bridge/ReShade and image workload. Persistent bypass, missing
effect, flicker or crash requires rollback and F8/Core logs plus exact version/
hash in a bug report. Bypass-induced high FPS is not a successful optimization.
Issues #277/#272/#273 remain open pending actual candidate retesting.
