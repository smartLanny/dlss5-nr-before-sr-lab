# 0.5.1 inbox continuation: scoped retirement admission

## Identity and access boundary

Input: PR #300, `codex/d21-unified-next-20260920`,
`3e94329b18d7ab1da16c46fd62011ccdc04e8ddc`. Public **0.5.1 Beta** and internal
**beta0.5-dline21-unified10** are the SAME candidate. Published build source:
`d36dc7712d2683ed944ae98562afffbb3cebc47c`; documented Chinese Core SHA256:
`8251517994422ca11075fb055c6974f8296b9d174be5363520375e08237fe9b7`.
These identities were read online; the latest local Windows binary/dirty
checkout could not be inspected in this task.

The requested Windows connector repeatedly returned `503: OAuth token request
failed` before execution; a separate authorized SSH attempt timed out. No
security/authentication settings were changed. Work continued in an isolated
Mac checkout of the actual current online source. The old Mac checkout and its
uncommitted evidence were left intact.

**The three new raw Windows inboxes and their ETLs were not read or parsed.**
File retrieval exposed an older Perf10 report, not these materials; it was not
substituted for new 0.5.1 measurements. The next table is current Issue-record
content, distinct from direct raw-file findings or this task's source tests.

## Keep the reports separate

| Entry | What the online record supports | Still needs the actual inbox |
|---|---|---|
| #277 EMT | unified10; lifecycle-isolation after about one NR success; historical success is not current activity | First lifecycle fault and same-process Create/Evaluate/Reset/Execute/release sequence. Never clear isolation to make NR run. |
| #277 小小墨瑟 | Latest correction: nvngx_dlssnr.dll loaded, nrchain_nvngx.dll not loaded, NR success zero | bridgePresent/Class/Win32, exports/init result. Not-loaded alone is not proof of missing file; path/dependency/init failure remain distinct. |
| #298 你是360 | unified10, RTX 4080 SUPER, NR active, memory/utilization complaints | Actual selected layers, current scope/adoption, private commit and DXGI local/nonlocal Usage/Budget, same-process ETL. Requested layers versus an unrelated/stale fullNR counter is not a leak proof. |
| #305 Stellar Blade | Registered archive is Lockperf2; fixed Raven dialogue flicker reported across versions; logs but no F8 listed | Actual build/hash, scene marker, NR adoption and final output/Reset events. High NR success does not prove stable final pixels; flicker does not by itself prove an NR toggle. |

Historical D13-OFF/D21-OFF paths in #277 predate this candidate. Do not label them
0.5.1 traces because an ETL-PATHS file points at them. Read the user's actual
pointer file and any newer traces once access returns. Keep captures; no new
recording is needed merely to replace evidence that has not yet been opened.

## Already present in #300

The cumulative source contains Perf12 and reviewed #303
`1874a22a35376b6efa70cf15541cf6ebbcf1fb0d`: shader precompilation, immutable
pipeline/successful-format caches, delayed snapshot accounting, auxiliary
history Reset/reuse and dependent-child retirement. Do not implement these again.
The loader already records file presence, load classes, Win32 errors, missing
exports, init stage and controlled path fallback; this patch does not replace
those with a guessed model-download workaround.

## Reproduced source defect

FindOrCreateContext retires an incompatible old model, then calls
DeferNrCreationForReleaseLocked. The old helper collects releasable objects,
checks the destruction queue and outside-lock releases, but ignores retired
objects still in g_contexts because recording/fence/slot/binding proof is pending.

An empty destruction queue could therefore admit a replacement while the old
model remained allocated. The old regression explicitly allowed this coexistence.
This is a source-proven allocation-overlap gap, not proof of an unlimited leak,
and not a claim to explain every customer's memory/performance complaint.

The expanded test executes ACTUAL production Find, collector, drain, route
BlockSource/Reject and lifetime predicates with CPU OS/NGX service doubles.
It retains two fixed negative controls:

- `cace3e645b4ca76b42d8fe86e58266b3dd95b3c0`: ready-context overlap, expected exit 3.
- `3e94329b18d7ab1da16c46fd62011ccdc04e8ddc`: 0.5.1 in-flight overlap, expected exit 3.

Before editing runtime source, the current candidate compiled and reproduced:
`REPRODUCED: in-flight retired model still occupies memory when replacement context is admitted`.
This is an executable admission failure, not a replay of the player's ETL or a
measured NGX/VRAM allocation amount.

## Repair and safety boundaries

The helper now receives the requested carrier handle. New allocations defer
while retired contexts of that carrier remain, including older feature
generations and dependent auxiliary layers after their parent moved out.
Existing queued/outside-lock release and uncertain-lifetime checks are unchanged.
Matching live contexts return before this new-allocation gate, including an
already allocated different route for the same carrier.

While deferred, keep native SR/RR and the saved route, expose the existing
ContextReleasePending reason, and block same-source fallback from allocating
another model at Present. Do not add a CPU fence wait, flush, timed release,
forced eviction or saved request queue. The next call uses the latest requested
size/tuning. An unrelated carrier is not blocked solely by an unqueued retired
object belonging to this source; existing global release exclusion remains.

Diagnostics distinguish retiredInScope from queuedOrReleasing. The Chinese/
English visible message explains GPU completion and asks for F8 if persistent.
The log remains bounded. Only a NEW-context request scans the existing 16-entry
array; matching steady-state evaluation gains no new polling or lock.

This is not inactive-route cache eviction or a host-commit/VRAM admission budget.
Normal cached routes may still remain resident. No model history, active layer
count, extent, shader, HDR, exposure, jitter, MV/depth or FG algorithm is reduced.
Unproven/quarantined resources are never released. A permanently unresolved old
lifetime can keep the affected source bypassed, and changing configurations can
show a longer native-only interval instead of overlapping allocations. That
tradeoff needs Windows/game validation. Cold Feature18 initialization is unchanged.

## Validation performed

Host: Apple clang 21, macOS arm64. Installed Command Line Tools/SDK are selected
only through process-local environment variables; no Xcode license accepted or
machine configuration changed.

- The real admission test ran red on unmodified 0.5.1. After the repair, both
  controls return expected 3 and the current source passes with AddressSanitizer
  and UndefinedBehaviorSanitizer.
- Coverage: incomplete/device-removed fences; recording, pending, Signal, slot
  and binding blockers; both quarantines; five independent layers; detached
  Release reentry; same/other-carrier matching reuse; F6 cache; generation
  change; 32 rapid replacement requests and latest settings.
- Inherited auxiliary selector checks pass 128 history epochs, 14 storage/
  lifetime contracts and five-layer independent tuning/Reset. The actual
  guide/context regression passes, with one existing pragma-once compiler warning.
- 21 generator/staging/workflow Python tests pass with MULTIAPI_CORE_SOURCE
  pointing at the REAL modified source. The first invocation had one environment-
  gated skip; the final invocation ran it. Format overlay shader bytes remain
  unchanged in that test, not a claim about rebuilt Windows shader binaries.

Runners omit Windows-only static linking on other hosts; Windows flags otherwise
remain. History-only stubs accept the scoped signature; the real retirement gate
is exercised separately, not replaced by those stubs. Source pins for both changed
production inputs are updated. The existing complete unified Windows builder
already invokes the expanded admission gate.

```sh
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
export SDKROOT="$(xcrun --show-sdk-path)"
export CPLUS_INCLUDE_PATH="$SDKROOT/usr/include/c++/v1"
python3 scripts/validate-context-release-admission.py --compiler "$(xcrun --find clang++)" --sanitize --output build/<fresh-admission-output>
python3 scripts/validate-aux-history-reuse.py --compiler "$(xcrun --find clang++)" --output build/<fresh-history-output>
python3 scripts/validate-guide-history.py --compiler "$(xcrun --find clang++)"
MULTIAPI_CORE_SOURCE=src/nr_before_sr.cpp python3 -m unittest tests.multi_api.test_format_patch tests.multi_api.test_integration tests.test_lockperf2_workflow -v
```

**Verified: source behavior and CPU harnesses only. Not verified here: full
Windows Core compile, WARP, NVIDIA, rendered UI, new inbox parsing or game FPS/
VRAM savings.** No new DLL, candidate name or OTA receipt was minted. Published
0.5.1 files remain unchanged; integration must freeze a new identity before build.

## Resume on Windows

1. Read the latest local checkout/dirty state/build-info before merging this
   online-based branch. Preserve owner work and use an isolated worktree.
2. Read all named inboxes and ETL-PATHS.txt; bind each report to exact process,
   Core hash, runtime errors and time. Do not mix previous-process F8 appendices,
   old export paths or filenames as operational state.
3. EMT: find the FIRST lifecycle fault without removing quarantine. 小小墨瑟:
   classify the actual bridge load failure; keep the GPU-appropriate NR model.
4. #298: correlate context live/retired/queued state with process commit,
   Usage/Budget and matching frames; CPU samples are not FPS or VRAM measurements.
5. #305: inspect final scene continuity alongside NR activity/Reset, without
   treating this memory patch as a demonstrated flicker fix.
6. Run the full unified builder with inherited graphics/temporal/face/lifetime
   gates and WARP negatives, then package with a new frozen source/hash. Do not
   distribute a source-only experimental DLL as an accepted player candidate.
7. Targeted game check: same scene/settings single/multilayer, a genuine size/
   Create-parameter change with old work in flight, and F6 toggles. Old objects
   must survive their own proof, replacements must not overlap, matching activity
   must remain usable. Persistent bypass/flicker/crash needs F8/log/hash and
   rollback, not automatic retries. Original requested evidence need not be re-recorded.

Public API background, separate from the source test: D3D12 apps own resource
lifetime until GPU completion. DXGI process CurrentUsage exceeding Budget can
cause stutter; neither fact proves these reports' low utilization cause.

- https://learn.microsoft.com/en-us/windows/win32/direct3d12/porting-from-direct3d-11-to-direct3d-12
- https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info
