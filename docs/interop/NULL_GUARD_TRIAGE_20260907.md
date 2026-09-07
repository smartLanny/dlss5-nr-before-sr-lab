# P0/P1 guarded-null follow-up (2026-09-07)

Base: `main@7d17c10d8abf0b6342b0bcf4ff0e4a1e164647d3`.
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
- The dev3 raw TXT is identified in that comment, but its advertised 20260907
  directory is absent from the checked main tree. The ROG connector could not
  be reached. Only the published dev3 summary was read; do not imply a full
  raw-TXT or GPU replay. The older #99 test1 successful window is a different
  build/session and is not a dev3 acceptance result.
- P1 #64 / #38: the checked-in `bug-inbox/issue-64/20260907-*` and
  `bug-inbox/issue-38/20260907-*` fix1 verdicts/capture metadata identify
  pre successes followed by post-SR rejection with `dynamic=0x8`, a real Reset,
  covered guard, no taint, and no vertex/stream masks. The #64 native blend
  components were not present in that older capture schema. #38's small pre
  visual delta is a separate observation, not proof of working post NR.

## Ranked, falsifiable hypotheses

1. An observed blend component fails the existing bounded replay policy. The
   auto-raster path already admits the blend bit; adding that bit again cannot
   fix this. A ready, covered, untainted `dynamic=0x8` sample should report either
   `null-guard-blend-nonfinite` or `null-guard-blend-out-of-range` when the native
   four values violate that policy. Read the existing dev3 F8 `blendObserved`
   and `blend` fields first; `requested.blend` is the NR strength, not those
   four D3D12 values. No particular rejected value is established by the
   available older capture.
2. Another raster/descriptor invariant is missing in an actual post-SR sample.
   This should produce the corresponding viewport, scissor, owned-descriptor
   or generic guard reason, rather than the two blend-specific reasons.
3. Samples from different lists, modes or lifecycle phases were mixed. Compare
   the same-token/reset-serial tail, capture scope, provider and current mode;
   one startup `installing=1` / `native-hooks-unavailable` is not a terminal
   outcome. Do not count historical pre success as post/RR success.

## Minimal patch

`RasterNullRejectionReason` is the single source of the existing auto-raster
admission predicate and its failure detail. `CanAutoRestoreRasterNull` retains
its boolean interface; Capture publishes the detail only after higher-priority
hook coverage, unknown-state, taint and fresh-Reset checks have been applied.

New rejection details distinguish nonfinite/out-of-range blend, invalid
viewport/scissor and missing owned RTV/DSV copies. The existing F8 command-state
reason and rate-limited log expose them without adding a second snapshot or
inventing default values. The user-visible safe bypass and feedback path remain.

**No admission is widened.** In particular the `[0,1]` blend bound is retained
as this project's current conservative policy, not newly asserted as a D3D12
API validity rule. No clamping of application state, fabricated Reset, dropped
state restore, extra submission, RR input replacement or resource-lifetime
change is introduced. This is a diagnostic fix, not a claimed game-unlock fix.

## Verification performed on the isolated Mac workspace

- `python3 -m unittest discover -s tests -p test_null_guard_reasons.py -v`
  extracts the actual production predicate and Capture reason expression into
  a host C++ harness. Before the patch: 681 checks, 21 diagnostic failures.
  After: 681 checks, zero failures. Valid blends remain admitted; invalid
  blends, unknown/tainted states, missing Reset, descriptors and unsupported
  bindings remain rejected. A later ready sample supersedes startup hook
  unavailability; the test does not alter the production install sequence.
- `python3 -m unittest discover -s tests -p test_core_feedback.py -v`: 32 pass.
- `python3 -m unittest discover -s tests -p test_feedback_report.py -v`: 15 pass.
- `git diff --check`: pass.

These are source/host-predicate and feedback tests. No Windows addon compile,
package, new WARP run, NVIDIA execution or game acceptance was performed.
Hosted CI is reported separately on the PR; an unstarted job is not a pass.

## ROG acceptance

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
