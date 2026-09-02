# Sanitized test evidence

This file contains bounded observations, not claims of universal compatibility.
Raw logs are intentionally excluded because they contained installation paths and
process addresses. Evidence from different build timestamps is kept separate.

## Current candidate qualification

- The 2026-09-02 source snapshot builds successfully for Chinese and English
  add-on variants plus the forwarding bridge.
- Focused source review covered master-off bypass, RR ownership/exclusion,
  render/output extent resolution, high-resolution MV conversion, failed-create
  handle quarantine, and command-list/queue lifetime tracking.
- The newest lifetime changes pin a slot before native submission and signal a
  private fence on the actual submitting queue immediately after submission.
- These facts qualify the tree as an early bug-fix candidate only. They are not a
  substitute for a fresh rapid-toggle, feature-recreation, multiqueue, and
  resolution-change runtime regression.

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
- The run continued for more than 16,000 successful evaluations.

## Cyberpunk path tracing plus Ray Reconstruction

- Feature 13 was passed through unmodified for more than 22,000 evaluations in
  the recorded launch.
- No Feature 18 create or successful Mode 1/Mode 2 evaluate was recorded during
  that launch.
- A short F6 press disabled the master switch and another short press restored
  it. That historical run predated the process-lifetime-detour change; the
  current build uses an atomic native-SR fast bypass and still needs its own
  in-game regression.

## RTX 4070 historical report

- D3D12 and UE-style `R11G11B10_FLOAT` SceneColor.
- The format-conversion path reached FP16 successfully.
- Runtime init succeeded, but both Mode 1 and Mode 2 create attempts returned
  `0xBAD00001 FeatureNotSupported` across the recorded flag/quality variants.
- This was an older build and is evidence of a compatibility boundary, not proof
  of the precise internal model/kernel limitation.

## Evidence gaps

- No local run has yet validated a title whose exposure is supplied only through
  a non-null ExposureTexture.
- Non-zero Color/Output subrect bases need a dedicated current-build regression.
- Dynamic resolution with more than 16 distinct extents needs a stress test.
- RTX 40 behavior needs a current-source, text-only metadata/log comparison that
  records runtime signature status without sharing the runtime itself.
- The new command-list/queue fence retirement path is compile-verified but still
  needs rapid mode switching, feature recreation, and resolution-change stress
  testing in a current game build.
- Mode 1 compute-state interference needs per-title validation because full D3D12
  state restoration is not generically implemented.
- A real same-slot multiqueue submission needs a dedicated fail-closed regression;
  detection cannot undo work already accepted by a second queue.
