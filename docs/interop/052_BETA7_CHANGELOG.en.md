# 0.5.2 Beta 7: changes since 0.5 D21 (in development)

**Not delivered.** Beta 6 remains installed. Delivery requires a controlled, identical-input private SR quality improvement. Recorded work, submitted optical flow and software checks do not establish game quality or performance. See the [development acceptance record](052_BETA7_ACCEPTANCE.md) for passed, failed and pending evidence.

## Changes in this batch

- Private SR uses one CPU/shader floor-quarter contract for odd grids. Controlled same-frame B/Z, carrier, tail, private output and final composition captures separate downsampling loss, quantization and real inference. The output-domain high-frequency tail is not added back as a repair.
- Post-private SR uses carrier range 1 while retaining exact neutral channels from range 4 and their small residual tails. Exact same-pixel zero edits preserve the original, preventing support leakage. Shared RR encoding is unchanged. Controlled real inference improves dark areas, text and frame-to-frame variation; some fine lines remain worse than ordinary spatial recovery and game quality needs maintainer acceptance.
- The 4K AMDOF creation budget uses the pinned SDK allocation layout, retaining the cap and retirement checks. Admission logs distinguish budget, guide, source and lifetime rejection. Native MV after native RR still lacks the required same-source guide proof and remains rejected.
- The existing nine-group button warms up for 180 valid samples (20-second limit), measures 600 valid GPU samples (60-second limit), then saves up to three screenshots per group. Insufficient or unsupported groups record a reason and continue; completion and cancellation restore temporary plugin settings. Native without NR has no plugin GPU work to measure, so its report states not applicable and keeps screenshots.
- Internal session GPU interval and memory reports leave the public DLL ABI unchanged. Samples bind configuration, source frame, workload and completion fence, excluding creation, reset, bypass, switching and screenshots. Reports give median, P95, sample count and actual coverage. Callback intervals and incomplete GPU spans are not whole-game-frame costs.
- Native RR, ordinary NR, private Feature1, adaptive/low-frequency temporal modes and no accumulation have separate controls. Sequences cover static scenes, panning, moving objects, disocclusion, cuts, reflections and HUD. Real Feature1 improvement, game noise/ghosting and added whole-frame cost ≤3% remain separate acceptance requirements.

## Carried forward from D21 and upgrade limits

Picture/lighting/experimental controls, rebindable shortcuts, one to five independently configured NR layers, once-per-chain work scaling, proven-source temporal/private SR paths and the Beta 6 ReShade private-call interface correction are retained. Private SR and external accumulation remain mutually exclusive. New experiments default off; saved INI, jitter and settings are preserved. On first migration from D21, the old auxiliary scale is archived and auxiliary layers use the primary work grid.

The tools do not switch game SR/RR/HDR/FG. Private Feature13 RR remains outside near-term candidates; cross-RR edit transport and NVOF stay later research. Core packaging does not mix NR/SR/RR/FG runtimes or face assets and does not change Manager defaults. Beta 6 and previous rollback packages remain available.

After development gates pass, 6sol will back up and deploy only after NTE exits. The maintainer runs game acceptance, NTE first and Cyberpunk 2077 last, including Esc/cuts, Present, map power saving and multilayer dark scenes. No main merge or online OTA publication is included.

Source comparison starts at 0.5 D21 `cbfb33919696a2840ed5154c91abaea56228b661` (historical identifier `beta0.5-dline21-223fix2`). A formal delivery manifest must establish final build/package SHAs and hashes; this note does not make an intermediate build deliverable.
