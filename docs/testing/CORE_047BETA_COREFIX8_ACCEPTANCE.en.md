# 0.4.7beta-corefix.8 retest notes

This candidate addresses format evidence lost during descriptor copies. Copies beyond the observer's per-slot recording limit leave their destinations unknown while preserving untouched view records. Destinations remain unusable throughout the copy, and overlapping old writes cannot become valid after it returns. The game retest must establish whether sustained NR returns and whether flicker remains.

## Zenless Zone Zero: one POST retest

1. Exit the game and back up the current Core. Install the matching language Core through the confirmed Manager external-runtime deployment path. Preserve INI, HoYoShade, ReShade, NR runtime, and picture settings. Load one Core in the process.
2. Enter the original 3D scene, select POST (SR→NR), and observe whether enabled NR remains visible.
3. Keep the scene running for about 30 seconds and press F8 once. If unsupported Output format returns, capture that state.
4. Return that one F8 report and describe sustained effect and remaining flicker. Do not repeat PRE or a four-phase test.

## Contents and restoration

The package contains one language Core, the matching `nrchain_nvngx.dll`, licenses, and provenance. It contains no settings, models, NVIDIA runtime, or carrier. Prefer replacing only the Core through the confirmed Manager deployment flow; avoid old shortcuts leading to another environment. `build-info.json` records the frozen source and local validation limits.

To restore, exit the game and restore the backed-up Core. Preserve earlier candidates, settings, and feedback.

## Validation boundary

Local CPU and WARP checks can verify view records, rejection during copies, current-call checks, and texture readback. They do not establish NVIDIA NR, final game visuals, or performance acceptance. CPU format requests remain separate from GPU descriptor-use proof.

Death Stranding still lacks sustained effect and occasionally flashes. This iteration does not deploy to that game or request another four-phase test.
