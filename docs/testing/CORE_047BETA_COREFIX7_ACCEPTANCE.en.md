# 0.4.7beta-corefix.7 retest notes

This candidate fixes a slot-selection defect: when Color/Output stays unchanged while motion-vector textures rotate, the old code can repeatedly select a busy slot and skip NR. It now prefers an existing correct binding, then a slot that can be changed safely. Descriptors still in GPU use cannot be overwritten. This retest must establish whether the change removes the in-game flicker.

## Zenless Zone Zero: one retest in the current POST scene

1. Exit the game and back up the current Core. Replace it with the matching language Core in the previously confirmed HoYoShade loading directory. Load only one Core in the process.
2. Keep the existing route, INI, ReShade, NR runtime, and picture settings. Enter the current original POST scene and observe whether NR remains visible while enabled.
3. You may turn NR off and on once. Keep the panel visible for about 30 seconds, then press F8 once. If the unsupported Output format message appears, capture that state; do not repeat a PRE test.
4. Return only this single F8 report and describe whether NR is visible, whether flicker remains, and whether unsupported format returns.

## Contents and restoration

The package contains one language Core, the matching `nrchain_nvngx.dll`, licenses, and provenance. It contains no INI, models, NVIDIA runtime, or carrier. Follow the confirmed HoYoShade installation method and replace only the Core where possible; verify an existing bridge with `SHA256.json` without adding another loader. `build-info.json` records the frozen source and local verification limits.

To restore, exit the game and restore only the backed-up Core. Preserve previous candidates and feedback.

## Unresolved scope

Death Stranding still lacks sustained visible NR and occasionally flashes. This iteration does not replace that game's Core, request another four-phase test, or establish that the display issue is fixed.

CPU format requests, GPU descriptor use, actual storage, and final display are separate facts. Local CPU/WARP checks do not replace real NVIDIA, game-visual, or performance acceptance.
