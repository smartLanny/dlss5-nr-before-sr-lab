# beta0.4.1-r2 feedback test build quick install

Press **F8** or click Collect feedback ZIP in the addon panel. Keep the camera still and wait for completion. `feedback.zip` normally goes under Documents/DLSS5-Feedback. If that directory cannot be created, collection tries DLSS5-Feedback under the system temporary directory; copy the actual path from the panel. A partial bundle includes missing-item reasons and can still be submitted. For an ordinary startup problem, use `Collect-Feedback.cmd`. After a crash, use `Collect-Crash-Feedback.cmd`; it derives one unique absolute game EXE path from the newest ReShade log and reads only existing Windows events, WER reports and dumps from the last 72 hours that pass its identity rules. It does not enable dumps, modify the registry or upload. Standalone CrashDumps do not contain their source path, so they can only be filtered by exact EXE basename and time; the package records this limitation.

The current full package contains **nine files**. Verify beta0.4.1-r2 in the panel, log and file properties, plus SHA-256. Collection stays local and never uploads or changes settings automatically.

This version fixes the Cyberpunk 2077 four-channel half-float motion-vector bypass at the default NR quality setting and restores null-PSO insertion to a conservative default bypass to reduce the Forza Horizon crash regression surface. Automated GPU, resource, state and capture regressions pass; this does not establish real-game acceptance for every exposure, colour-follow, face-quality or crash report.

Add-on: **DLSS 5 AI Rendering Upscaling Edition beta0.4.1-r2**

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
   - `DLSS5-AI-Rendering-Upscaling-beta0.4.1-r2-@野生的装机宅-Bilibili.en.addon64`
   - `nrchain_nvngx.dll`
   - `nr_before_sr.ini`
   - `nvngx_dlssnr.dll` from the matching GPU package
   - `Collect-Feedback.cmd`
   - `collect-feedback.ps1`
   - `Collect-Crash-Feedback.cmd`
   - `collect-crash-feedback.ps1`
5. Start the game and verify that the ReShade Add-ons page shows beta0.4.1-r2, the
   author and official release address at the top of this add-on's panel.

The add-on loads its configuration, bridge and NR runtime from its own folder.
If ReShade can see only the `.addon64` while the sibling files are elsewhere,
the panel may open but every option will appear ineffective.

## Quick use

- `F6`: master toggle. Off must be native game SR with zero Feature 18 calls.
- `F8`: keep the current settings by default, collect one display screenshot,
  and attempt to include 16 consecutive NR processing frames, one separate
  overview, effective parameters and logs. Processing frames and the screenshot
  are not guaranteed to share a frame. Only an explicit comparison selected in
  the advanced feedback controls temporarily changes in-memory settings.
- Main keyboard `-` (numpad subtract also works): save a screenshot. Its short
  name records build, chain, active/bypass state, requested configuration and
  `I/S/B/C/HP` (NR intensity, style, blend, colour follow, peak-shoulder experiment).
  Full extents, format and controls go into the matching log entry to avoid
  capture failures in long game paths. Names describe requested settings; use
  the log or stage metadata for the actual NR instance, configuration and frame.
- The default performance `NR -> SR` option runs the model on an independently
  selected NR working raster, composites its matched residual back into the
  game's render-resolution colour, then calls the game's native SR. NR quality
  does not change the game's own DLSS mode.
- Turn that option off for the full-output-resolution `SR -> NR` quality and
  performance reference path.
- While Ray Reconstruction owns a live feature, the release candidate reports
  `RR safe bypass` and does not stack a second NR pass.
- Committing Intensity, Style, mask, skin or local controls retires the old
  Feature 18 after its exact GPU fence and creates a new one. A short safe
  warm-up/bypass interval during this switch is expected.
- New installations use Final Effect Strength 1.2. The 0-to-8 slider is the
  actual request with no hidden multiplier. Raising it amplifies NR changes
  relative to the original image and can also amplify grain at a low NR working
  resolution; raise NR quality before deciding whether high strength is useful.
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
matching window from `nr-before-sr.log`. beta0.4.1-r2 logs state-machine changes,
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

For simple copy-in installation, each current full package contains **nine files**:
the add-on, bridge DLL, configuration, matching NR runtime, this README, the
ordinary offline collector pair `Collect-Feedback.cmd` / `collect-feedback.ps1`,
and `Collect-Crash-Feedback.cmd` / `collect-crash-feedback.ps1` for reading only
existing crash evidence.
Separate signature files, verification scripts, and duplicate documents are no
longer bundled. If the official release post publishes a ZIP SHA-256, that value
can be used to check the downloaded archive.

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
again. This waiting period is not proof of network convergence. F8 uses its own
continuous small-region and overview format; the full F7 stage request remains
separate and is never presented as the same frame as the display screenshot.
Keep the camera fixed, change one parameter at a time and use metadata to
distinguish requested values, effective values and colour domains.
