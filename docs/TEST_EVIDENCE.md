# Sanitized test evidence

This file contains bounded observations, not claims of universal compatibility.
Raw logs are intentionally excluded because they contained installation paths and
process addresses. Evidence from different binary hashes is kept separate. Current
builds use a stable embedded build ID rather than compile time so clean rebuilds can
be checked for byte-for-byte reproducibility.

## Current source/build qualification (2026-09-04)

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
- Two clean beta0.3.3-dev-r2 builds produced identical artifacts. Their SHA-256
  values are `EFAEDEE32193038725D61F6E5B7FFE60827D7FF7A5A16977C14DACDACCAC2D56`
  (Chinese), `290F6C5448736F2F9064FBCF40609050E7BB67B33F93544D3762E06B1A276433`
  (English), and
  `46041A5FF91AE2FD907E310D132AABC3C4A1ECD48DACE511B8672909D5D9C2FB`
  (bridge). This is build qualification only: these exact add-on hashes have
  not yet completed the gameplay gate.
- The four embedded codec entry points validate with shader model 5.0, and the
  LLVM static analyzer passes the English add-on, Chinese add-on and forwarding
  bridge targets. The shader-model change is specifically intended for games
  shipping an older `d3dcompiler_47.dll` that rejects `cs_5_1`.
- A Feature 1 Create that overlaps delayed lifecycle-hook publication is now
  detected from its entry and in-flight counters. Failed Create output slots
  are cleared before the native call, unknown non-null Release handles fail
  closed, and permanently detoured NGX modules are pinned until process exit.
  These are source/build checks; the opaque-call RR transition boundary remains
  documented and cannot be proved away by static analysis.

## Latest gameplay-tested binary runtime status

The beta0.3.2 development Chinese binary with SHA256
`4E483F25DF943DE78DA8D77CB0A2ABB23C951BAEAFF7B6605A36E6A150FB1AF1`
completed two fresh Cyberpunk 2077 direct-launch runs on 2026-09-03.

- With RR off, the observed contract was `2560x1600 -> 3840x2400`. The default
  `NR -> SR` path exceeded 1,800 carrier evaluations, including 1,792 Feature 18
  successes at the 1,800 checkpoint. Transfer prepared/recorded/bound/submitted
  were `1790/1790/1790/1790`; lifecycle bypass was zero and no actual queue
  mismatch, device removal, runtime rejection or duplicate-list detection was
  logged. Sanitized local evidence hash:
  `5ED337901A40C3E47153FD62C311AB5BB448DD9ADC62C88C6F9A67F4C23156FC`.
- With RR on, Feature 13 exceeded 1,800 evaluations and Feature 18 create/evaluate
  count remained zero. The frame sampler changed to the explicit
  `A1-native-RR-bypass` stage, and there was no observer uncertainty or late
  module gap. Sanitized local evidence hash:
  `53869AF559C2F9A0387ABF606C4188BE32D39A0F00B5C5C3961C60E897D77751`.

A preceding source-equivalent build, before only the RR performance-stage label
changed, completed five live order transitions. Another beta0.3.2 build completed
five Enabled off/on cycles; every off interval stopped new Feature 18 work. These
support the state transitions but are not claimed as exact-final-hash evidence.

This proves the confirmed beta0.3 startup crash remains fixed locally, the
default chain reaches sustained execution, and RR exclusion works on the exact
tested binary. The only subsequent add-on source change is replacement of volatile
compile date/time text with a stable build ID; that makes clean builds reproducible
but does not transfer an exact-hash gameplay claim to them. This evidence does not
replace the physical F6, DLSS-preset, Hogwarts, or Witcher gameplay/visual gates.

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

## Witcher contract and fixed-scene HDR ablation

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
- A separate beta0.3.1 HDR report used `PreExposure=1`, `ExposureScale=1`, no
  ExposureTexture and FP16 input/output. Its original-to-proxy mean luminance was
  approximately `-0.06%`, while Feature 18's mean output was `-13%` to `-22%`
  and the final Mode-2 graft was `-5%` to `-7%`. `PreserveLighting` was disabled.
  This evidence points to a broad neural luminance residual rather than the old
  asymmetric exposure/codec bug.
- The exact current Chinese beta0.3.2-dev-r2 add-on, SHA256
  `1E717CB5D2170D36EBDAD078B052E96DBB0BDD3084F512889C82DF9A1757F560`,
  completed a four-way fixed-camera gameplay capture on 2026-09-03: native SR,
  Mode 2, Mode 2 plus Lighting Match, and Mode 1. The live contract was
  `1114x696 -> 1920x1200`, FP16 input/output, `PreExposure=1`,
  `ExposureScale=1`, and no ExposureTexture.
- In the Mode-2 interval without Lighting Match, ColorDiag attributed the shift
  to the neural residual: NR-versus-proxy mean luminance was approximately
  `+18%` to `+20%`, and the render-resolution composited result was about
  `+16%` to `+17%` versus the original. With Lighting Match enabled, the same
  diagnostic reduced the composited mean error to about `+0.2%` to `+0.4%`.
- The captured 8-bit PNGs contain no ICC or HDR metadata, so they are only a
  matched relative comparison. Linearized screenshot means nevertheless show
  the same direction: uncorrected Mode 2 was about `+20.5%` over native SR for
  the full frame; Lighting Match was about `-4.1%`. Across selected static
  regions the corrected result was about `-2.4%` to `-4.6%` versus native SR.
  Mode 1 matched the full-frame native mean within `-0.1%`, while individual
  sign/window regions still moved by approximately `+26%` and `-27%`. This
  demonstrates that Feature 18 can change local lighting style even when a
  global exposure statistic is stable.
- No lifecycle uncertainty, device removal, queue mismatch, or failed Feature
  18 creation was found in the captured run. The game was frame-limited near
  60 FPS, so this capture is not performance evidence. Evidence snapshot SHA256:
  `DFC42DD29AC34B5FECEE0C17E73A46083A7EB8C6070949830C6273F5F408A4E4`.
  The four labelled PNG hashes are recorded in the local test archive and are
  intentionally not committed to the source repository.
- A later beta0.3.3-dev-r1 HDR-toggle log isolated a separate state-machine
  defect. The game attempted a reversed `2560x1440 -> 1707x960` Feature 1
  Create, received `InvalidParameter` with a null handle, and evaluated that
  failed path before successfully creating `1707x960 -> 2560x1440`. r1
  permanently labelled the null/unknown Evaluate an RR ownership gap, so the
  later valid handle recorded zero carrier evaluations. r2 explicitly keeps
  null handles on the native invalid-call path, separates failed attempts from
  successful Feature-1 activity, and publishes delayed lifecycle hooks in one
  queued transaction. Real unknown non-null handles remain fail-closed. This is
  deterministic log evidence for the bug and build evidence for the correction;
  a live r2 HDR off/on transition remains required.

## Cyberpunk path tracing plus Ray Reconstruction

- Historical evidence passed feature 13 through for more than 22,000
  evaluations. The exact current beta0.3.2 development binary additionally
  passed more than 1,800 feature-13 evaluations with zero Feature 18 work and
  reported the dedicated native-RR bypass performance stage.

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
