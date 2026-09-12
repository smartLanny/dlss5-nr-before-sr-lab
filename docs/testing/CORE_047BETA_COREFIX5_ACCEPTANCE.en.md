# 0.4.7beta-corefix.5 acceptance notes

This candidate follows the corefix.4 game failures. It corrects activity resets caused by changing drawing threads, retains descriptor evidence across unrelated writes, adds explicit native format requests for reused views, and compares legal UNORM storage results at matching sample coordinates. Game results require new feedback from this candidate.

The package contains one language Core, the matching `nrchain_nvngx.dll`, licenses, and provenance. Exit the game, back up the current Core, and replace it in the actual loading directory. Load only one Core. Preserve existing INI, ReShade, NR runtime, and picture settings. Verify the bridge hash if the matching bridge is already installed. `build-info.json` and `SHA256.json` identify the frozen source and delivered bytes.

## Death Stranding

Keep the existing **SR → NR (post)** route and reproduction scene. Select **Normal sampling** on the feedback page, keep the panel visible for about 30 seconds, then press F8. Report whether NR remains visible, whether brief flashes persist, and whether recent activity still returns to checking. One normal-sampling report is sufficient for the new storage comparison.

Paused sampling still permits NR but produces no new automatic GPU storage samples. Late readbacks retain their original phase. Returning to checking after all UI readers have stopped for more than two seconds remains expected.

## Zenless Zone Zero

Replace the Core in the previously confirmed HoYoShade loading directory. Follow the original reproduction steps into an actual scene and press F8 after about 30 seconds. Keep the existing route and settings. Report whether unsupported format remains and whether NR stays visible. Do not force an Output format manually.

## Results and restoration

Recent processing, native format requests, actual storage, and final display are separate facts. A complete sparse storage match does not establish final presentation. Local CPU, WARP, and menu checks do not replace NVIDIA or game acceptance.

To restore, exit the game and restore only the backed-up Core. The dedicated delivery preserves the previous Core, installation receipt, and restoration preflight. Earlier candidates and feedback remain available.
