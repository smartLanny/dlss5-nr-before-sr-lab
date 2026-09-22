# unified8 candidate: menu and focused acceptance

No automatic installation or stable release. Keep unified7, your INI and the NR
model appropriate for this GPU. The two model builds serve different GPU families;
this package does not compare or replace them. Consult the delivery receipt for
the exact built commit and separately recorded validation levels.

## Changes

- Feedback → Guided acceptance applies temporary plugin settings, then restores
  them on completion, cancellation or stopped presentation. No INI/model changes.
- Deep stage capture may include native SR output O from the same pre-NR call.
  Missing, unknown or unfinished stages are explicit; another frame is never used.
- Nine Present, dual spatial, native-guide and RR-depth shader entries are
  precompiled. PSO and NR model creation still cost time and are reported separately.
- Bounded pre-SR SDR and post-RR SDR AMD temporal entries; see
  [exact route contracts](UNIFIED8_TEMPORAL.md). Native pre/RR MV and HDR remain unavailable.

Colour algorithms, model defaults and workload are unchanged. Conservative is
the default; Colour Priority retains a separate initial strength of 0.7. Saved
values survive. Temporal, private reconstruction, independent faces and both
energy modes remain OFF for new configurations.

## Guided acceptance

Set game RR/HDR/FG manually BEFORE starting. Do not change them during a sequence.
Each group waits for actual configuration/adoption, at least 3 seconds and 64
successful post-reset evaluations, then measures 20 seconds. Unavailable warmup
is recorded after 20 seconds. F8 and optional deep capture occur AFTER timing.

| Program | Temporary changes |
| --- | --- |
| Current settings | None; repeat with identical settings on unified7/candidate, restarting the game to replace Core |
| Colour and scale | Temporal OFF; current route/model/policy; 100%, 99%, minimum; then original scale with AI colour 0/0.7/1 |
| Native / AMD temporal | Same model/layers/scale; OFF, Native, AMD, and later-layer scope where available |

Minimum pre scale is 50%, post 25%. The controller never increases layer count.
Deep captures reset history and incur extra work outside the performance window.
Unsupported groups and incomplete captures are not a pass.

The output directory contains acceptance.txt, callback CSV and available group
F8 reports; large stage folders are indexed. Callback intervals are NOT generated
FPS or GPU duration. Missed samples, NR submissions, latest primary source/reset
and normalization multiplier are explicit. VRAM is sampled process usage/budget;
the reported peak is only the sampled peak. Recording is not a quality verdict.

## About one hour

1. NTE, 22 min: short equal-settings version baseline, colour/scale and switches.
2. Where Winds Meet, 12 min: equal-workload OFF/Native/AMD; lighting, motion,
   flicker, trails and detail.
3. Wuthering Waves, 10 min: RR enabled, actual projection adaptation and NR success;
   capture F8. Failed admission must retain native RR.
4. Cyberpunk, 12 min: manual RR/FG/HDR; baseline plus supported SDR temporal routes.
5. Summary, 4 min: verify restoration and send indexed records/anomalies.

Only failed scenes need another run. No model A/B or exhaustive manual matrix.
Private SR/RR and NVOF are not exposed as connected features in this package.
Bypass, lost layers or weaker enhancement cannot establish a performance gain.

CPU, full Core, WARP, controlled NVIDIA, hosted CI and game evidence are separate.
Issues #246/#254 require corresponding repaired-game evidence. Roll back outside
the game by restoring the unified7 Core and matching nrchain; keep only one Core
within all add-on search paths.
