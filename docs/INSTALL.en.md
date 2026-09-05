# beta0.3.8-hd2-exp1 experimental feedback build quick install

Press **F8** or click Collect feedback ZIP in the addon panel. Keep the camera still and wait for completion. `feedback.zip` normally goes under Documents/DLSS5-Feedback. If that directory cannot be created, collection tries DLSS5-Feedback under the system temporary directory; copy the actual path from the panel. A partial bundle includes missing-item reasons and can still be submitted. If the game cannot start, double-click `Collect-Feedback.cmd` beside `collect-feedback.ps1`. This offline collector creates `feedback-offline.zip` under Documents and needs no administrator privileges.

This is the separate HD2 Feature 11 lifecycle experiment and has not passed an HD2 compatibility test. The core full-install payload contains **seven files**; an official signed archive also includes verification files listed in its manifest. Verify `beta0.3.8-hd2-exp1` in the panel, log and file properties, plus SHA-256. Collection stays local and never uploads or changes settings automatically.

This version retains the non-finite exposure fallback, descriptor, strength-migration and UI fixes, and adds duplicate-NR detection, a low-address NGX hook relay-allocation fix, and module preflight before the first SR Create. The Forza report has no crash dump, so only the reduced startup risk surface is established; each game still needs retesting.

Add-on: **DLSS 5 AI Rendering Upscaling beta0.3.8-hd2-exp1 [Feature11 experiment; unverified]**

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
3. Remove old `nr-before-sr*.addon64` builds and the other language variant.
   If the exact file `renodx-dlss5.addon64` is present, move it outside the game
   directory too; it is another NR owner and cannot coexist with this add-on.
4. Keep these files **together in that same directory**:
   - `DLSS5-AI-Rendering-Upscaling-beta0.3.8-hd2-exp1-@野生的装机宅-Bilibili.en.addon64`
   - `nrchain_nvngx.dll`
   - `nr_before_sr.ini`
   - `nvngx_dlssnr.dll` from the matching GPU package
   - `Collect-Feedback.cmd`
   - `collect-feedback.ps1`
5. Start the game and verify that the ReShade Add-ons page shows
   `beta0.3.8-hd2-exp1`, its experimental status, the author and official
   release address at the top of this add-on's panel.

The add-on loads its configuration, bridge and NR runtime from its own folder.
If ReShade can see only the `.addon64` while the sibling files are elsewhere,
the panel may open but every option will appear ineffective.

## Quick use

- `F6`: master toggle. Off must be native game SR with zero Feature 18 calls.
- `F8`: collect three display screenshots with the current settings, one set of
  NR stages from the same frame, effective parameters and logs. The three
  screenshots are spaced about two seconds apart; **this is not automatic A/B**.
  Change the toggle, chain or style yourself and collect a separate bundle from
  the same camera position. Display screenshots and NR stages are not from the
  same frame.
- Main keyboard `-` (numpad subtract also works): save a screenshot. Its short
  name records build, chain, active/bypass state, requested configuration and
  `I/S/B/C/HP` (NR intensity, style, blend, colour follow, peak-shoulder experiment).
  Full extents, format and controls go into the matching log entry to avoid
  capture failures in long game paths. Names describe requested settings; use
  the log or stage metadata for the actual NR instance, configuration and frame.
- The default `render-resolution NR -> SR` option follows the game's real DLSS
  render extent, then passes the result into the game's native SR.
- Turn that option off for the full-output-resolution `SR -> NR` quality and
  performance reference path.
- While Ray Reconstruction owns a live feature, the release candidate reports
  `RR safe bypass` and does not stack a second NR pass.
- Committing Intensity, Style, mask, skin or local controls retires the old
  Feature 18 after its exact GPU fence and creates a new one. A short safe
  warm-up/bypass interval during this switch is expected.
- Start with NR Intensity 1. In the current original 310.8 runtime, the path
  used here without an external control mask does not amplify the model beyond
  1. A setting can be applied successfully without a stronger 1-to-2 response.
  This revision does not substitute extra darkening or contrast gain for it.
- `Scene Paper-White Scale` controls HDR normalization before NR; values above
  1 feed a darker normalized scene to the model. Format-24 HDR input is first
  PQ-decoded, uses this paper-white scale, and is symmetrically encoded back to
  PQ. It is neither final display brightness nor NR intensity, so start at 1.
- The advanced peak-shoulder experiment is off by default and is only for
  controlled captures of admitted HDR input. It has not been shown to improve
  Spider-Man faces or NR-to-SR quality; leave it off for normal testing.

For a report, submit the F8 ZIP and describe the game, trigger and expected
result. Use the offline collector if the game cannot start; partial bundles
with missing images or logs can still help. If packaging fails, include the
displayed version and bypass reason plus the smallest
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

For simple copy-in installation, the core full-install payload contains **seven files**:
the add-on, bridge DLL, configuration, matching NR runtime, this README, and the
offline collector pair `Collect-Feedback.cmd` and `collect-feedback.ps1`.
Official signed archives additionally carry one source record, manifest and
signature, public key, offline verifier and verification notes so every payload
file can be tied to the frozen commit. The manifest is the exact inventory; the
release page also publishes the ZIP SHA-256.

## Parameters and stage diagnostics

The panel distinguishes requested settings, the last applied NR intensity/style
and warmup status. Colour Follow and Lighting Match show current effective values
and restriction reasons; without a current valid contract record, they show
unknown. Restricted requested values are retained for a later compatible mode.
Unknown HDR contracts still do not enable colour follow. Visible style/intensity
differences still require controlled scene comparisons.

Optional “Capture NR stages once” exports four raw floating-point PFM images
and metadata under `nr-stage-captures` beside the log. It is off by default.
NR-to-SR capture stops before native SR. Use `-` for a separate displayed-image
screenshot. Press `F7` to request the stages independently (one pending request
at a time). The four stages share one composition frame, but do not share the
display screenshot's frame. Stage capture recreates NR history, waits for 64
successful evaluations, exports after the actual GPU fence and recreates history
again. This waiting period is not proof of network convergence. F8 attempts to
include the stages in the feedback ZIP. Keep the camera fixed, change one
parameter at a time and use metadata to distinguish requested values, effective
values and colour domains.
