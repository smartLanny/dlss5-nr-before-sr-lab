# Sanitized test evidence

This file contains bounded observations, not claims of universal compatibility.
Raw logs are intentionally excluded because they contained installation paths and
process addresses. Evidence from different build timestamps is kept separate.

## Current source/build qualification (2026-09-03)

- Chinese and English add-on variants plus the forwarding bridge build cleanly;
  all four embedded shader entry points compile successfully. Exact binary
  hashes belong to each candidate package manifest so later source-only fixes
  cannot accidentally inherit an older hash.
- Both language variants are checked with the LLVM static analyzer. Their Windows metadata,
  ReShade exports, overlay identity and external filenames contain `beta0.3`
  and `@野生的装机宅 Bilibili`; resource text was rebuilt with UTF-8 input and
  read back without mojibake.
- The current source additionally serializes command-list affinity markers,
  ignores non-direct queues, creates retirement-fence state only for batches
  that actually contain a lifecycle token, uses a locked ColorDiag snapshot,
  and distinguishes transfer `prepared`, `recorded`, and native-queue
  `submitted` counts.

## Current-binary runtime status

The current lifecycle-marker and submitted-counter revision has build/static
qualification only. It has not completed a fresh native gameplay gate. Earlier
runtime evidence below remains useful regression guidance, but it must not be
reported as evidence for this exact binary.

## Pre-current Cyberpunk candidate

An earlier beta0.3 candidate, before the final affinity-marker serialization and
three-stage transfer accounting, produced these bounded observations:

- Cyberpunk `NR -> SR` reached 596/600, 1,196/1,200 and 1,796/1,800 successful
  Feature-18/carrier evaluations (99.33%, 99.67%, 99.78%). The earlier strict
  single-command-list candidate reached only 339/1,200 (28.25%); the new ratio
  diagnostic exposed that regression and the multi-list same-queue admission
  corrected it.
- Cyberpunk `SR -> NR` reached 598/600 successful Feature-18/carrier
  evaluations (99.67%) at `3840x2400` with `1920x1200` guides.
- Five Enabled off/on cycles were applied while Feature 1 was active. Every off
  interval froze the Feature 18 counter and every on interval resumed it. The
  run recorded no actual queue mismatch, lifecycle uncertainty, device removal,
  runtime rejection, or slot exhaustion.
- A separate RR/path-tracing interval exceeded 2,400 feature-13 evaluations and
  logged zero Feature 18 work. This supports the RR exclusion design but does
  not replace a current-binary rerun.

## Earlier Hogwarts candidate observations
- Hogwarts Legacy rebuilt the live Feature 1/Feature 18 contract on every
  launch and followed the requested DLSS mode: Performance `1920x1200`,
  Balanced `2228x1392`, and Quality `2562x1601`, all targeting `3840x2400`.
  Each run used the exact per-Evaluate active extent rather than assuming only
  the Create scalar.
- A Performance run created one `1920x1200` context for `NR -> SR` and
  one `3840x2400` context with `1920x1200` guides for `SR -> NR`; both exceeded
  600 successful evaluations. Balanced exceeded 600 and Quality exceeded 480
  in `NR -> SR`. High-resolution motion was converted from `3840x2400` to the
  active render extent while retaining the game's `MV.Scale=(1,1)`.
- The Hogwarts diagnostics showed that the source-relative residual reached
  native SR: after warm-up, nearly every sparse sample had a non-zero final
  change. Performance `NR -> SR` sampled around 6.2% mean source-relative delta;
  `SR -> NR` sampled around 4.4--5.0%. These are diagnostic magnitudes, not a
  perceptual-quality score or a claim that the two processing orders match.
- A candidate master-off interval began at cumulative `feature18Evals=1990`;
  after about 46 seconds, re-enable still logged exactly `1990`. This is direct
  runtime evidence that the native bypass executed zero Feature 18 work.
- Five processing-order transitions and five Enabled off/on cycles completed on
  that candidate. No crash, device removal, lifecycle uncertainty, runtime
  rejection, or slot exhaustion was recorded.
- The earlier same-process Hogwarts comparison measured about 84.5--84.9 FPS on
  the native-SR fast path, 41.2--41.4 FPS for render-resolution `NR -> SR`, and
  27.2--27.3 FPS for output-resolution `SR -> NR`. It is retained only as a
  bounded scene observation; it is not a universal performance claim.

The three core runtime paths therefore have prior-candidate evidence only. The
settings watcher uses the same master state and bypass branch as F6, but the
physical F6 entrypoint, three-title rerun and requested preset cycles remain
open for the current hash. This remains a candidate rather than a completed
beta0.3 release.

## Wuchang fixed-scene lighting regression

- Game SR input/active region observed near `1279x800`; output `3840x2400`.
- Mode 2 Feature 18 create/evaluate succeeded at the render extent.
- Scalar exposure observed as PreExposure `4.0`, ExposureScale `1.0`, with no
  ExposureTexture.
- Fixed-region mean luminance:
  - plain SR: `69.601`;
  - earlier Mode 2: `73.924` (`+6.2%`);
  - Mode 2 with PreserveLighting: `68.864` (about `-1.1%`);
  - Mode 1: `69.765`.
- Same-process performance was approximately 89 FPS plain SR, 68 FPS Mode 2,
  and 27 FPS Mode 1. Cross-launch values were not used for overhead conclusions.

## Witcher Mode 1 contract

- Game SR contract observed near `1114x696 -> 1920x1200`.
- Output-resolution Feature 18 create succeeded at `1920x1200` with
  `1114x696` depth/motion guides.
- Historical evidence continued for more than 16,000 successful evaluations.
- A 2026-09-03 pre-current candidate independently exceeded 600 Mode 1 evaluations,
  then completed five order transitions and five Enabled off/on cycles. Every
  disabled interval logged the same Feature 18 counter before and after; no
  lifecycle uncertainty, device removal, duplicate context, or slot exhaustion
  was recorded.
- In the sampled scene, bounded display-linear mean luminance moved from about
  `0.0471` to `0.0496` (roughly +5%). The much larger per-pixel mean relative
  delta is dominated by near-black denominators and must not be read as a global
  30% exposure shift.

## Cyberpunk path tracing plus Ray Reconstruction

- Historical evidence passed feature 13 through for more than 22,000
  evaluations. Several pre-current beta0.3 candidates repeated the exclusion
  test for 1,200--5,400 feature-13 evaluations with zero Feature 18 work.
  The current hash still needs the requested RR rerun.

## RTX 4070 historical report

- D3D12 and UE-style `R11G11B10_FLOAT` SceneColor.
- The format-conversion path reached FP16 successfully.
- Runtime init succeeded, but both Mode 1 and Mode 2 create attempts returned
  `0xBAD00001 FeatureNotSupported` across the recorded flag/quality variants.
- This was an older build and is evidence of a compatibility boundary, not proof
  of the precise internal model/kernel limitation.

## Evidence gaps

- A GTA V Enhanced report exposed a 1x1 `R32G32B32A32_FLOAT`
  `ExposureTexture`; the previous candidate bypassed those frames and only ran
  intermittently when the pointer was absent. The new same-command-list
  red-channel path compiles and its four shader entry points validate, but it
  still needs a fresh run on that title before it counts as runtime evidence.
- Non-zero Color/Output subrect bases need a dedicated current-build regression.
- Dynamic resolution with more than 16 distinct extents needs a stress test.
- RTX 40 behavior needs a current-source, text-only metadata/log comparison that
  records runtime signature status without sharing the runtime itself.
- The command-list/queue fence retirement direction has bounded pre-current
  Cyberpunk/Hogwarts same-queue evidence, mode/Enabled cycles and three clean-
  launch resolution contracts. The current affinity-marker revision still
  needs the minimum gate, same-process feature recreation, dynamic-resolution
  and real multiqueue stress testing.
- Mode 1 compute-state interference needs per-title validation because full D3D12
  state restoration is not generically implemented.
- A real same-slot multiqueue submission needs a dedicated fail-closed regression;
  detection cannot undo work already accepted by a second queue.
- The physical F6 hotkey route and three-preset cycles in Cyberpunk/Witcher
  remain pending. Hogwarts completed Performance, Balanced and Quality.
- Palworld was updated to the current candidate and attached successfully, but
  its unattended title screen also produced zero feature-1 carrier frames; it
  is an optional follow-up, not release evidence.
