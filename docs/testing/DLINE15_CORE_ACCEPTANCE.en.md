# 0.5 D15 test build: installation and use

This `beta0.5-dline15` package updates an existing DX12 Core installation. It
contains one language Core and its matching `nrchain_nvngx.dll`. Keep your INI,
ReShade, NVIDIA model/runtime and game files. See `build-info.json` and
`SHA256.json` for the exact build identity and hashes. Local validation, game
compatibility, final presentation and whole-frame performance are separate results.

## Update and restore

1. Exit the game and back up the current Core and `nrchain_nvngx.dll` outside the game directory.
2. Put both packaged modules in the existing Core directory. Remove the previous
   Core version from that directory and keep exactly one Core language installed.
3. Keep your INI, ReShade and the game's original NVIDIA DLLs. Confirm D15 in the menu.
4. To restore, exit the game, remove both new modules and restore the matching backup pair.

A fresh installation still requires the complete original installation environment.
The matching D15 bridge can initialize NGX in a game without native DLSS
initialization. It includes the official SDK loader; keep Core and bridge
together. The NVIDIA driver and separately supplied signed NR runtime remain
required. See `LICENSES.txt` for the embedded SDK terms.

## Starting position and full-size reference

The three buttons request Before, After or Present. A Before admission failure
with no private work can advance to After, then Present. An After request tests
only After and Present. The saved preference and actual position are displayed
separately. Fallback persists for the session; clicking a start button again or
restarting retries the selection. Ordinary picture or resolution changes do not
automatically promote the route.

Present prioritizes verified native guides from the same source frame. It
automatically uses color-only processing when guides are unavailable and resets
history per source frame; compare that mode separately. Processing before FG also requires proof of
the current source and consumer boundary. Recorded or GPU-completed work alone
does not establish the final displayed image.

Full-size reference is off by default. Enable it to request 2, 3 or 4 independent
passes at the first pass size, followed by one final composition. Requested and
adopted counts are distinct. Your previous reduced second-pass fraction is kept
and returns when full-size reference is disabled.

## Suggested test sequence

Use a fixed scene. Capture an F6-off native baseline, then compare one pass, each
starting position, full-size 2/3/4 passes and the saved reduced second pass.
Where available, test native SR, RR, no DLSS, FG off/on and SDR/HDR separately.
Record picture quality and whole-frame performance separately. If output is
unexpected or processing keeps waiting, press F8 and retain the report, exact
switching sequence, game version, API, resolution, SR/RR/FG/HDR settings and scene.

F8 remains the status/log report. On a compatibility path, F7 captures the current
complete processing layers under Documents in `DLSS5-Present-Capture` or
`DLSS5-FG-Capture`, according to the actual path. The UI distinguishes pending,
exporting, failed and exported requests. These files are not a native-stage
matrix or final-screen screenshots.

Format issues #253/#202/#248 and exposure #228 retain their existing shared
mechanisms and require same-build game results. #247 and #68 are historical
comparisons, not evidence of D15 compatibility.
The [detailed game acceptance worksheet](DLINE15_GAME_ACCEPTANCE.zh-CN.md) is provided in Chinese.
