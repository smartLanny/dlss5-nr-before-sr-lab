# P0/P1 guarded-null follow-up (2026-09-07)

Base: `main@db0577c565d0a81a6f5c010b35698e078adf1205`.
The former multiapi-045 integration history, including dev3 `b03f474`, is already
an ancestor. This is a temporary branch targeting main, not a new release line.

## Evidence and limits

- P0 #99 / #15: the dev3 report in #99 comment 5569032596 records
  `requested.mode=1`, `workMode=5`, start/end `command-state-unverified`,
  `null-guard-graphics-state-used`, `scope.nrSuccess=0`, historical NR success
  5030, RR attempts 4191 and RR NR recordings 0. The historical totals do not
  prove current post/RR success. This capture does not demonstrate a currently
  selected pre-NR failure, and zero live RR handles alone does not prove that
  RR never ran.
- The archived dev3 raw TXT was later read on ROG during #154 validation. Its
  same sample records `blend=-0,-1,-2,-3`, `dynamic=0x8`, fresh Reset, covered
  guard, no taint and no PSO. The older #99 test1 successful window remains a
  different build/session and is not a dev3 acceptance result.
- P1 #64 / #38: the checked-in `bug-inbox/issue-64/20260907-*` and
  `bug-inbox/issue-38/20260907-*` fix1 verdicts/capture metadata identify
  pre successes followed by post-SR rejection with `dynamic=0x8`, a real Reset,
  covered guard, no taint, and no vertex/stream masks. The #64 native blend
  components were absent from the older fix1 capture schema; the newer #64
  dx11-compat F8 records the same finite `-0,-1,-2,-3` factors. #38's small pre
  visual delta remains separate and does not prove working post NR.

## Ranked, falsifiable hypotheses

1. The observed finite `-0,-1,-2,-3` factors failed the former `[0,1]` project
   policy even though `OMSetBlendFactor` accepts finite floats. #154 removes
   that false bound, preserves the values bit-for-bit and retains a specific
   `null-guard-blend-nonfinite` rejection for NaN/Inf. `requested.blend` is the
   NR strength and is unrelated to these four D3D12 values.
2. Another raster/descriptor invariant is missing in an actual post-SR sample.
   This should produce the corresponding viewport, scissor, owned-descriptor
   or generic guard reason, rather than the nonfinite blend reason.
3. Samples from different lists, modes or lifecycle phases were mixed. Compare
   the same-token/reset-serial tail, capture scope, provider and current mode;
   one startup `installing=1` / `native-hooks-unavailable` is not a terminal
   outcome. Do not count historical pre success as post/RR success.

## Minimal patch

`RasterNullRejectionReason` is the single source of the existing auto-raster
admission predicate and its failure detail. `CanAutoRestoreRasterNull` retains
its boolean interface; Capture publishes the detail only after higher-priority
hook coverage, unknown-state, taint and fresh-Reset checks have been applied.

New rejection details distinguish nonfinite blend, invalid
viewport/scissor and missing owned RTV/DSV copies. The existing F8 command-state
reason and rate-limited log expose them without adding a second snapshot or
inventing default values. The user-visible safe bypass and feedback path remain.

The blend admission now accepts every finite float and still rejects NaN/Inf.
Values are replayed unchanged. No application state is clamped; no Reset is
fabricated; state restoration, submission, RR inputs and resource lifetimes
remain unchanged. This is not a claimed game-unlock fix.

## Integrated Windows verification

- Integrated source `fa658f5b986498354c6b6097f94a5172f03d5976`
  preserves all #154 and #149 ancestors. Its final command-state blob is
  `f0f13b0a8ac243f01cdf0f93a517440ac65f1394`, also pinned by the candidate
  generator.
- `python -m unittest discover -s tests -p test_null_guard_reasons.py -v`
  extracts the actual production predicate and Capture reason expression into
  a Windows C++ harness: 693 checks pass. Finite out-of-unit values are admitted
  unchanged; NaN/Inf and all other existing guards remain rejected with the
  same Capture priority.
- Core feedback 32 tests and feedback report 15 tests pass.
- `validate-044-compat.py`: 38 forwarding and 172 production null-raster checks
  pass, including sticky-registration/UI integration.
- Native command-state: 66 checks, 64 KiB worker and 198-function stack gate;
  its default-null suite reports 718 checks and 73 GPU checks.
- WARP: 24 command-state scenarios, three paired native/restored finite blend
  pixel cases, and the intentionally wrong blend negative control pass.
- The exact-source candidate builder reports 35 gates, complete staging,
  static analysis, bilingual core/nrchain/carrier compile and all included WARP
  suites passing. `validation.json` SHA256 is
  `7659d120a2951012342ec784138a0dd2913a246a7543d675cebe0af4071dc27f`.

These are local Windows compile and WARP results. NVIDIA Feature18 and game
acceptance were not performed. Hosted CI is reported separately on the PR; an
unstarted job is not a pass.

## Remaining game acceptance

First read the already archived dev3 F8 files for #99; do not ask the player to
upload them again. Preserve build/source/addon identity and inspect same-sample
blend values and final reasons, not the first installation line. Then build
this candidate locally and run the native command-state and WARP restoration
harnesses, including the bad-blend negative control, before a game retest.

Use the same scenes for #64/#38 pre to post and #99 ordinary SR versus native RR.
Confirm current-scope NR/record/submit/completion counters, real provider/mode,
F6 visual difference and F8 tail. RR must remain native when post state cannot
be restored safely. A repeated bypass must remain visible and direct the user
to feedback collection. Keep #99/#15/#64/#38 open; no game or RR compatibility
claim follows from the diagnostic regression passing.
