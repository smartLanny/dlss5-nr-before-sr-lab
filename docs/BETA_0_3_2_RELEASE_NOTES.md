# beta0.3.2 deferred-module and diagnostic candidate

beta0.3.2 is a focused compatibility/debugging candidate built on the
beta0.3.1 startup-stability baseline. It does not change the two processing
orders or automatically alter a game's exposure. Mode 2 (`NR -> native game
SR`) remains the default performance path; Mode 1 (`native game SR -> NR`)
remains the full-output quality/reference path whenever the live contract is
accepted.

Project author: **@野生的装机宅 Bilibili**  
Only official release location: https://space.bilibili.com/941799

## Fixed or strengthened

- Added a once-per-second in-memory observer for `_nvngx.dll` and
  `nvngx_dlssd.dll`. Distinct D3D12 Create/Evaluate/Release exports that appear
  after startup are hooked without rescanning the filesystem.
- A fully covered RR-capable module discovered before the first observed
  Feature-1 Create/Evaluate no longer permanently disables the add-on. This is
  the specific cause found in the supplied Wuthering Waves beta0.3.1 log: the
  modules loaded several seconds before the game's first DLSS SR feature, but
  the old observer treated their mere late arrival as an unrecoverable gap.
- Late discovery after Feature-1 activity, incomplete lifecycle coverage, module
  replacement, or a distinct DLSSD Evaluate with an unknown handle still fails
  closed. The UI distinguishes temporary observer installation from permanent
  ownership uncertainty.
- Game-owned auxiliary NGX handles, including Feature 11 frame generation, are
  now classified too. If a distinct late `_nvngx` Evaluate receives a handle
  that was never seen by any hooked Create, the process fails closed instead of
  assuming a pre-existing RR handle cannot exist.
- The same unknown-handle guard now covers the outermost public SR Evaluate and
  direct DLSSD Evaluate paths. This closes a startup-observation gap where a
  feature could have been created before its owning module received complete
  lifecycle hooks; the UI asks for a clean restart rather than processing an
  uncertain frame.
- Added direct optional hooks for distinct `nvngx_dlssd.dll` Create, Evaluate
  and Release exports. RR Feature 13 remains strictly pass-through; its live
  handle causes companion Feature-1 work to remain native with zero Feature 18.
- Corrected a false `nr_before_sr.ini is not writable (Windows error 2)` warning.
  The Win32 profile-cache flush form returns zero by contract and is no longer
  treated as a failed write. Concrete key writes remain checked individually.
- Module diagnostics now include rescan count, coverage, startup-deferred
  admission, first Feature-1 Create count, carrier count and observer-gap state.
- ColorDiag now records the exact sampled mode, detail transfer, colour-follow
  and Lighting Match state. It logs original/proxy/neural/composited mean RGB,
  mean luminance and hashes plus a threshold-based stage attribution.
- RR performance samples are labelled `A1-native-RR-bypass` instead of being
  incorrectly grouped with active `NR -> SR` samples.
- Uncapped loading screens no longer emit several complete performance windows
  per second; unchanged epochs publish at most one 600-frame window every five
  seconds.
- Concurrent auxiliary swapchain Presents no longer race cadence/F6 edge state,
  and the performance sampler reads an immutable in-memory configuration copy.
- Main keyboard `-` (and numpad subtract) now asks ReShade to save a diagnostic
  screenshot. The file name records the exact build, chain, active/bypass state,
  input/NR/output extents, output format and live image controls without adding
  any work to the Feature 18 command-list transaction.

## Witcher 3 HDR finding

The supplied beta0.3.1 Witcher 3 log does not match the old asymmetric HDR
codec failure. Its source and proxy mean luminance differed by only about
`-0.06%`, while Feature 18's neural output was roughly `-13%` to `-22%` darker
and the final graft about `-5%` to `-7%` darker. The reported run had
`PreExposure=1`, `ExposureScale=1`, no ExposureTexture, FP16 storage and
Lighting Match disabled.

This points to a broad luminance component in the neural residual. beta0.3.2
does not hide it with an unconditional exposure gain: that would brighten valid
output in other games and can clip HDR highlights. The gameplay-validated r2
binary completed a fixed-scene ablation. In that second scene the uncorrected
neural residual moved in the opposite direction (`+18%` to `+20%`), confirming
that a fixed global gain cannot be correct. Lighting Match reduced the
render-resolution composited mean error from roughly `+16%` to below `+0.4%`;
matched final screenshots placed selected static regions within about `2%` to
`5%` of native SR. It remains off by default because this is one scene, its
sparse local correction costs additional texture loads, and its face/edge/local
contrast cost still needs a dedicated capture.

## Runtime evidence and remaining gates

The exact Chinese add-on binary with SHA256
`4E483F25DF943DE78DA8D77CB0A2ABB23C951BAEAFF7B6605A36E6A150FB1AF1`
passed fresh Cyberpunk 2077 runs on the maintained RTX 50 system:

- RR off: more than 1,800 Feature-1 carrier evaluations; the 1,800 checkpoint
  reported 1,792 Feature-18 successes and aligned
  prepared/recorded/bound/submitted counts, with no lifecycle or device error.
- RR on: more than 1,800 Feature-13 evaluations, zero Feature-18 create/evaluate
  work, and the explicit native-RR bypass performance label.

All four embedded shader entries compile. Chinese, English and bridge sources
build successfully, and both add-on language variants plus the bridge pass the
focused LLVM static analyzer. Two clean r3 builds were byte-identical; the
Chinese, English and bridge hashes are
`F88518...1C9D8C`, `FD650B...CE8591`, and `DB26E4...11B60`. The exact Chinese r2
hash completed the Witcher fixed-camera four-way run described above, with more than
600 frames in every active mode and no logged lifecycle/device error.
Wuthering Waves remains a supplied-log/source-fix item. Hogwarts/current-hash
visual testing, the remaining physical F6/order cycles and three live DLSS
preset changes are still open. This is therefore a development/community-test
candidate, not a universal stable release.

## Packaging boundary

The source repository and signed source manifest do not contain vendor/community
runtime binaries. Generated full archives contain one pinned runtime: the RTX 50
archive uses an Authenticode-valid NVIDIA 310.8 build, while the shared RTX 30/40
archive uses ShortFuse `310.8.SF.0` with SHA-256
`6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927`.
Every included file is covered by the package's project-signed manifest.
