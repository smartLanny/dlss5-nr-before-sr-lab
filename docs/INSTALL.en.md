# beta0.3.3 tuning and R8 compatibility candidate quick install

Add-on: **DLSS 5 AI Rendering Upscaling Edition beta0.3.3-dev**

Author: **@野生的装机宅 Bilibili**

Only official release location: [野生的装机宅 on Bilibili](https://space.bilibili.com/941799). The author and official address remain embedded in the add-on UI and binary.

## Scope

- 64-bit D3D12;
- a native DLSS Super Resolution call (NGX Feature 1);
- ReShade/RHI configured to load `.addon64` files;
- offline, non-anti-cheat use only.

DX11, Vulkan, games without native DLSS SR, and paths fully replaced by a
translation layer such as OptiScaler are outside this add-on's direct scope.

## Install

1. Exit the game completely.
2. Open the Addon search directory actually used by ReShade/RHI. With no custom
   path this is commonly the directory containing the real 64-bit game EXE and
   the ReShade DLL.
3. Remove old `nr-before-sr*.addon64` builds, the other language variant, and
   any second DLSS5/RenoDX/ShortFuse add-on that would own NR at the same time.
4. Keep these files **together in that same directory**:
   - `DLSS5-AI-Rendering-Upscaling-beta0.3.3-dev-@野生的装机宅-Bilibili.en.addon64`
   - `nrchain_nvngx.dll`
   - `nr_before_sr.ini`
   - `nvngx_dlssnr.dll` from the matching GPU package
5. Start the game and verify that the ReShade Add-ons page shows beta0.3.3-dev, the
   author and official release address at the top of this add-on's panel.

The add-on loads its configuration, bridge and NR runtime from its own folder.
If ReShade can see only the `.addon64` while the sibling files are elsewhere,
the panel may open but every option will appear ineffective.

## Quick use

- `F6`: master toggle. Off must be native game SR with zero Feature 18 calls.
- Main keyboard `-` (numpad subtract also works): save a screenshot. Its file
  name records the build, chain, active/bypass state, DLSS input/NR/output
  extents, format, intensity, blend, style, mask, skin/local controls, Lighting
  Match and colour strength.
  `IN/NR/OUT` are the three extents; `F/I/B` are output format, NR intensity and
  blend; `AM/SK/LT/LS/LM/C/PW` are auto-mask, skin, local tone, local structure,
  Lighting Match, colour follow and Scene Paper-White Scale.
- The default `render-resolution NR -> SR` option follows the game's real DLSS
  render extent, then passes the result into the game's native SR.
- Turn that option off for the full-output-resolution `SR -> NR` quality and
  performance reference path.
- While Ray Reconstruction owns a live feature, the release candidate reports
  `RR safe bypass` and does not stack a second NR pass.
- Committing Intensity, Style, mask, skin or local controls retires the old
  Feature 18 after its exact GPU fence and creates a new one. A short safe
  warm-up/bypass interval during this switch is expected.
- `Scene Paper-White Scale` controls HDR normalization before NR; values above
  1 feed a darker normalized scene to the model. Format-24 HDR input is first
  PQ-decoded, uses this paper-white scale, and is symmetrically encoded back to
  PQ. It is neither final display brightness nor NR intensity, so start at 1.

For a report, include the displayed version and bypass reason plus the smallest
matching window from `nr-before-sr.log`. beta0.3.3 logs state-machine changes,
deferred-module coverage, colour-stage attribution, early diagnostics and sparse
performance summaries rather than writing every frame.
Remove user names, absolute paths, account data, process addresses and runtime
binaries before sharing.

For a global HDR brightness difference, hold one camera position and capture
about 15 seconds each with F6 off, default `NR -> SR`, and `NR -> SR` plus
Experimental Lighting Match. Do not change style, intensity or exposure during
that ablation; the log can then separate proxy-contract, neural-model and final
composition bias.

The RTX 30/40 ShortFuse full package already includes the matching
`310.8.SF-v2` runtime. Extract the package as one unit and do not enable a second
ShortFuse/RHI NR add-on at the same time.

## Package contents

For simple copy-in installation, each full package contains only five files:
the add-on, bridge DLL, configuration, matching NR runtime, and this README.
Separate signature files, verification scripts, and duplicate documents are no
longer bundled. If the official release post publishes a ZIP SHA-256, that value
can be used to check the downloaded archive.
