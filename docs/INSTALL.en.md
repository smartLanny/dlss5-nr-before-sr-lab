# beta0.4.3 feedback test build quick install

Press **F8** or use the core-feedback control to create a small CPU-only package with no screenshot or texture readback. Use **F7** only when paired image evidence is needed. Feedback ZIPs normally go under Documents/DLSS5-Feedback. If that directory cannot be created, collection tries DLSS5-Feedback under the system temporary directory; copy the actual path from the panel. Missing or limited inputs are reported explicitly as partial rather than being treated as complete. For an ordinary startup problem, use `Collect-Feedback.cmd`. After a crash, use `Collect-Crash-Feedback.cmd`; it derives one unique absolute game EXE path from the newest ReShade log and reads only existing Windows events, WER reports and dumps from the last 72 hours that pass its identity rules. It does not enable dumps, modify the registry or upload. Standalone CrashDumps do not contain their source path, so they can only be filtered by exact EXE basename and time; the package records this limitation.

The core full-install payload contains **nine files**. An official signed archive also includes verification files listed in its manifest. Verify beta0.4.3 in the panel, log and file properties, plus SHA-256. Collection stays local and never uploads or changes settings automatically.

This version fixes the Cyberpunk 2077 four-channel half-float motion-vector bypass, detects the exact competing `renodx-dlss5.addon64` module, and fixes the low-address NGX relay-allocation failure shown in a new log. It installs the narrow command-signature observer at device creation to fix the r3 intermittent bypass regression, while null-PSO and wider graphics-state paths remain disabled by default. The Forza report has no crash dump and still needs a game retest. Automated GPU, resource, state and capture regressions do not establish real-game acceptance for every exposure, colour-follow, face-quality or crash report.

Displayed add-on name: **DLSS 5 AI Rendering Perfect Chain beta0.4.3**.

The update filename keeps “AI-Rendering-Upscaling” for continuity with older packages.

Author: **@野生的装机宅 Bilibili**

Only official release location: [野生的装机宅 on Bilibili](https://space.bilibili.com/941799). The author and official address remain embedded in the add-on UI and binary.

## Scope

- 64-bit D3D12;
- a native DLSS Super Resolution call (NGX Feature 1);
- ReShade/RHI configured to load `.addon64` files;
- offline, non-anti-cheat use only.

DX11, Vulkan, games without native DLSS SR, and paths fully replaced by a
translation layer such as OptiScaler are outside this add-on's direct scope. The panel now identifies a D3D11 presenting runtime directly; an auxiliary D3D11 device inside a D3D12 game does not latch that warning.

## Install

1. Exit the game completely.
2. Open the Addon search directory actually used by ReShade/RHI. With no custom
   path this is commonly the directory containing the real 64-bit game EXE and
   the ReShade DLL.
3. Remove old `nr-before-sr*.addon64` builds and the other language variant.
   If the exact file `renodx-dlss5.addon64` is present, move it outside the game
   directory too; it is another NR owner and cannot coexist with this add-on.
4. Keep these files **together in that same directory**:
   - `DLSS5-AI-Rendering-Upscaling-beta0.4.3-@野生的装机宅-Bilibili.en.addon64`
   - `nrchain_nvngx.dll`
   - `nr_before_sr.ini`
   - `nvngx_dlssnr.dll` from the matching GPU package
   - `Collect-Feedback.cmd`
   - `collect-feedback.ps1`
   - `Collect-Crash-Feedback.cmd`
   - `collect-crash-feedback.ps1`
5. Start the game and verify that the ReShade Add-ons page shows beta0.4.3, the
   author and official release address at the top of this add-on's panel.

The add-on loads its configuration, bridge and NR runtime from its own folder.
If ReShade can see only the `.addon64` while the sibling files are elsewhere,
the panel may open but every option will appear ineffective.

## Quick use

- `F6`: master toggle. Off must be native game SR with zero Feature 18 calls.
- `F8`: create the small core-feedback ZIP. It keeps current picture settings,
  does not wait for a successful NR frame, takes no screenshot, performs no
  texture readback, and has a hard 8 MiB ZIP limit. Missing or rejected inputs
  are recorded explicitly as partial / missing-reason evidence.
- `F7`: run the image-feedback flow. It first creates a session-associated core
  package and then performs the heavier image/stage capture. A verified package
  pair proves identity consistency, not that both captures came from one frame.
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

For an ordinary report, submit the F8 core ZIP and describe the game, trigger
and expected result; add an F7 image package only when visual evidence is needed.
Use the offline collector if the game cannot start. Partial bundles can still
help, but do not edit their manifest. If packaging fails, include the
displayed version and bypass reason plus the smallest
matching window from `nr-before-sr.log`. beta0.4.3 logs state-machine changes,
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

For simple copy-in installation, the core full-install payload contains **nine files**:
the add-on, bridge DLL, configuration, matching NR runtime, this README, the
ordinary offline collector pair `Collect-Feedback.cmd` / `collect-feedback.ps1`,
and `Collect-Crash-Feedback.cmd` / `collect-crash-feedback.ps1` for reading only
existing crash evidence.
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
again. This waiting period is not proof of network convergence. F8 is CPU-only
core feedback and does not use the image-readback path; the full F7 stage request
remains separate and is never presented as the same frame as the display screenshot.
Keep the camera fixed, change one parameter at a time and use metadata to
distinguish requested values, effective values and colour domains.
