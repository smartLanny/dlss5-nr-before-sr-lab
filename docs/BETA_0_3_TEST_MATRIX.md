# beta0.3 sanitized test matrix

## beta0.3 minimum release gate

beta0.3 does not require the full seven-title RC matrix. Its blocking local
gate is limited to Cyberpunk 2077, Hogwarts Legacy, and The Witcher 3:

- Hold every exercised mode for 300–500 stable frames after warm-up.
- Toggle F6 five times, switch processing order five times, and change the
  game's DLSS preset three times per title.
- Require no crash, device removal, `lifecycle uncertain`, duplicate Feature 18
  owner/evaluation, or unresolved failed-create quarantine.
- F6 off and Cyberpunk 2077 RR-safe-bypass intervals must record zero Feature 18
  work.
- Cyberpunk 2077: RR off must run the selected chain; RR on must be an explicit
  safe bypass.
- Hogwarts Legacy: run UE `NR -> SR` and compare it against `SR -> NR`, recording
  ColorDiag and fixed-scene face/exposure/highlight differences.
- The Witcher 3: exercise the full-resolution `SR -> NR` post path.

Passing this section is sufficient to package beta0.3. The extended matrix
below is the RC/community qualification target.

### Current minimum-gate status

| Title | Current candidate evidence | Remaining blocker |
|---|---|---|
| Cyberpunk 2077 | Pre-current candidate: `SR -> NR` reached 598/600, `NR -> SR` reached 1,796/1,800, five Enabled cycles resumed, and an RR interval exceeded 2,400 feature-13 evaluations with zero Feature 18. | Full focused rerun of the current affinity-marker/submission-counter hash, three DLSS-preset changes and physical F6 |
| Hogwarts Legacy | Pre-current candidate: Performance `NR -> SR` and `SR -> NR` each exceeded 600 frames; Balanced exceeded 600 and Quality exceeded 480 in `NR -> SR`; live contracts rebuilt as 1920x1200, 2228x1392, and 2562x1601. A 46-second master-off interval kept the Feature 18 counter exactly at 1990. | Focused rerun of the current hash, physical F6 and fixed-camera image capture |
| The Witcher 3 | Pre-current candidate: mixed-resolution `SR -> NR` (`1920x1200`, guides `1114x696`) exceeded 600 frames; five mode transitions and five Enabled cycles completed; every off interval froze the Feature 18 counter; no lifecycle/device/slot error | Focused rerun of the current hash, three DLSS-preset changes and physical F6 |

The gate is therefore not yet complete and no release tag should be created.
Palworld is an optional UE follow-up after this table passes; it has been added
to the local queue ahead of Assassin's Creed IV: Black Flag.

## RC and community extended matrix

Use one fixed scene and identical game/camera/settings for four comparison
groups: RenoDX/RHI 4.70 pure SR, RenoDX/RHI 4.70 official post-SR NR, this
add-on's `SR -> NR`, and this add-on's `NR -> SR`. Capture at least 600 stable
frames after warm-up in every group. The official add-on and this add-on must
never be enabled together; when a separate launch is required, record it and
repeat the pure-SR control in each launch instead of presenting cross-launch
FPS as a same-process measurement.

## Required local coverage

| Title / scenario | Contract focus | 4.70 pure SR | 4.70 official post-NR | Our NR -> SR | Our SR -> NR | Required observation |
|---|---|---:|---:|---:|---:|---|
| Cyberpunk 2077, RR off | FP16, path tracing optional | Pending | Pending | Pending | Pending/contract-gated | No duplicate Feature 18; fixed-scene color/performance |
| Cyberpunk 2077, RR on | Feature 13 ownership | Pending | Game RR reference | Safe bypass | Safe bypass | Our Feature 18 evaluations exactly zero |
| Wuchang | UE active extents, high-res MV, exposure | Pending | Pending | Pending | Pending/contract-gated | No MV top-left crop; no global exposure drift |
| Hogwarts Legacy | UE format/exposure variation | Pending | Pending | Pending | Pending/contract-gated | Explicit format/exposure/bypass status |
| The Witcher 3 | Mixed-resolution post-SR guides | Pending | Pending | Pending | Pending/contract-gated | Create/evaluate contract remains stable |
| Clair Obscur: Expedition 33 | Repeated mode and feature recreation | Pending | Pending | Pending | Pending/contract-gated | No waiting-state regression or stale history |
| Wuthering Waves | R11 output conversion | Pending | Pending | Pending | Pending/contract-gated | Mode 1 no longer rejects solely on format 26 |

`Pending/contract-gated` means the path stays native until the current frame's
format, exposure, subrect, guide, view-capability and command-list contracts
have been accepted. The decision is not made from the executable name.

## RTX 40 community coverage

| GPU | Runtime record | Minimum scenarios | Status |
|---|---|---|---|
| RTX 4070 | SF-v2 byte size, SHA-256, signature state | One FP16 title and one R11 UE title | Pending |
| RTX 4090D | SF-v2 byte size, SHA-256, signature state | One FP16 title and one R8-format title | Pending |

R8 results remain native-SR bypass unless a color-contract profile confirms
linear or sRGB encoding. Reaching a format rejection is not evidence that the
external runtime failed.

## Per-title checklist

- Record game/build, GPU, driver, Windows build, add-on commit, runtime metadata,
  display size, DLSS preset, render/output active extents, formats, and RR state.
- Capture mean FPS, frame time, 1% low, GPU utilization, and VRAM for at least
  600 stable frames per mode, using the same scene and settings.
- For RC qualification, repeat F6 off/on, mode switching, DLSS preset switching,
  and feature recreation at least 20 times each.
- Confirm no crash, device removal, `lifecycle uncertain`, isolated dependency,
  slot exhaustion, stale context, or duplicate Feature 18 execution.
- Confirm F6 off is within run-to-run noise of an add-on-free control and records
  zero Feature 18 create/evaluate work.
- With RR active, confirm the UI reports `RR safe bypass`, feature 1/13 remain
  native, and Feature 18 evaluations remain exactly zero.
- Verify active subrect and non-zero-base bounds; change dynamic resolution or
  DLSS preset and ensure old resources retire only after successful reset.
- Recreate Feature 1 without changing dimensions (toggle HDR, fullscreen/window,
  or another setting that changes create flags). Confirm a new generation is
  logged and format/exposure/flags are revalidated rather than inherited.
- Compare fixed-scene screenshots for exposure, highlights, hue, faces, foliage,
  thin geometry, motion stability, disocclusion, and ghosting. Record expected
  low-resolution detail differences separately from global color drift.
- Record execution, native/warm-up, transfer and sampled GPU-delta counters.
  Capture all four ColorDiag stages (original, proxy, NR output, composited):
  mean RGB, luminance, hash, invalid sample count and maximum NR delta. Do not
  infer execution or the location of a color shift from appearance alone.

## beta0.3 release blockers

- Any Feature 18 call during F6 off or RR safe bypass.
- Crash, hang, device removal, unsafe reset/release, failed-create non-null handle
  reuse, or work submitted after lifecycle isolation.
- Unexplained format/color conversion, out-of-bounds subrect, alpha corruption,
  persistent global exposure/hue drift, or silent fallback without a reason.
- Material F6-off overhead or any Feature 18 work during the native control.
- An RTX 40 package containing SF-v2 before redistribution rights are confirmed.

The seven-title 600-frame-per-group and 20-cycle stress requirements are RC
blockers, not beta0.3 packaging blockers.

## Sanitization before sharing

Keep only the smallest matching log window and replace usernames, absolute game
paths, account IDs, machine names, process addresses, and unrelated module lists.
Do not commit game files, screenshots, dumps, vendor/community runtimes, model
payloads, or raw logs. Identify evidence by add-on commit plus a neutral test ID,
not by a tester's personal path or account.
