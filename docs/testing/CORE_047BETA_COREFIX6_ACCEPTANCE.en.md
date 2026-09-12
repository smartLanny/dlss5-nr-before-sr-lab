# 0.4.7beta-corefix.6 retest notes

This candidate addresses one confirmed observation gap in the corefix.5 Zenless Zone Zero feedback: a native Clear still has a known CPU view format, but eviction of the GPU descriptor record incorrectly discards the independent CPU format request. Eligible CPU requests are now retained and must still pass the exact current successful native-call gate. Known conflicts and unknown CPU formats remain rejected. Do not force an Output format. Game visuals have not been accepted.

## Zenless Zone Zero: one toggle retest

1. Exit the game and back up the current Core. Replace it with the matching language Core in the previously confirmed HoYoShade loading directory. Load only one Core in the process.
2. Keep the existing route, INI, ReShade, NR runtime, and picture settings. Enter the original reproduction scene and observe whether NR remains visible while enabled.
3. Turn NR off and on once in the game. Keep the panel visible for about 30 seconds, then press F8. If the unsupported Output format message returns, capture that state.
4. Return this single F8 report and describe whether NR is visible before and after the toggle, whether flashes remain, and whether unsupported format returns.

## Contents and restoration

The package contains one language Core, the matching `nrchain_nvngx.dll`, licenses, and provenance. It contains no INI, models, NVIDIA runtime, or carrier. Follow the confirmed HoYoShade installation method and replace only the Core where possible; verify an existing bridge with `SHA256.json` without adding another loader. `build-info.json` records the frozen source and local verification limits.

To restore, exit the game and restore only the backed-up Core. Preserve previous candidates and feedback.

## Unresolved scope

Death Stranding still lacked sustained visible NR and occasionally flashed with corefix.5. This iteration does not establish the final-display root cause, replace that game's Core, or request another four-phase test.

CPU format requests, GPU descriptor use, actual storage, and final display are separate facts. Local CPU, WARP, and menu checks do not replace real NVIDIA, game-visual, or performance acceptance.
