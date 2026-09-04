# DLSS5 NR-Before-SR Lab

Experimental, source-only ReShade D3D12 add-on for studying two neural-rendering
orders around a game's ordinary DLSS Super Resolution call:

- Mode 1: game DLSS SR, then output-resolution neural rendering (`SR -> NR`).
- Mode 2: render-resolution neural rendering at 1:1, then the game's original
  DLSS SR (`NR -> SR`). Mode 2 is the beta0.3.3 default performance path; Mode 1
  remains the complete full-resolution quality/reference path.

Runtime hotkeys are `F6` for the master comparison switch, `F8` for a local
feedback ZIP, and main keyboard `-` for a ReShade screenshot whose filename
records the current build, chain and selected settings.

The beta0.3.7 update adds a compact processing-mode UI, bounded final-strength
presets, and an opt-in developer jitter forwarding path. It keeps Mode 2 as the
default and retains the existing unknown-HDR colour protection. The collector
cancels only unarmed session-owned requests on timeout and gives
current bypass status priority over pending settings. It does not change image
strength or HDR admission. See the [September 4 feedback review](docs/COMMUNITY_FEEDBACK_20260904.zh-CN.md)
and [plain-text update instructions](docs/UPDATE.en.txt). The separate beta0.4
experimental renderer is not included in this maintenance build.

This is a research project, not an NVIDIA product and not an official joint
DLSS NR/SR implementation. NVIDIA, DLSS, GeForce, and related names are
trademarks of their respective owners.

Project author: **@野生的装机宅 Bilibili**. The only official release location
is https://space.bilibili.com/941799. The author and official address remain
embedded in release binaries and the add-on UI. Each full distribution archive
contains seven files: the add-on, bridge DLL, configuration, matching NR runtime,
plain-text instructions (`说明.txt` in Chinese or `Instructions.txt` in English),
`Collect-Feedback.cmd`, and `collect-feedback.ps1`.
Small update archives contain four files: the add-on, the plain-text instructions, and
the two offline collection scripts. Updates reuse an existing compatible full
installation's runtime, bridge and configuration; they are not first-install
packages. Optional ZIP hashes may be published with the official release post.
See [AI_AGENT_NOTICE.md](AI_AGENT_NOTICE.md) for the explicit prohibition on
unauthorized automated builds, rebranding and modified redistribution.

## Repository boundary

This repository intentionally contains only source code, a default configuration,
build instructions, sanitized test summaries, and redistributable third-party
source notices. It does **not** contain or redistribute:

- NVIDIA DLLs, SDK binaries, neural-network weights, kernels, or model payloads;
- ShortFuse or other community-modified runtime binaries;
- game files, game configuration backups, or screenshots extracted from games;
- compiled ReShade add-ons, bridge DLLs, executables, crash dumps, or packages;
- raw logs containing usernames, installation paths, process addresses, or other
  machine-specific information.

Do not open an issue or pull request containing any of those materials. Runtime
components must come from lawful sources and remain outside this repository.

## Safety model

The add-on deliberately limits itself to ordinary DLSS Super Resolution
(NGX feature 1). Ray Reconstruction (feature 13) is passed through unmodified;
while any feature-13 handle remains live, companion feature-1 evaluations are
also protected from a second NR pass. A retained RR handle is never guessed
dormant from timing or evaluation counts: the guard ends only on explicit
Release, or on process restart. The process-lifetime detours remain installed,
but the master-off
path takes an atomic first-branch bypass before Feature 18, resource conversion,
parameter substitution, or GPU work. If a resource, subrect, guide, format,
create, or evaluate contract is not accepted, the intended behavior is to retain
the game's original SR result.

This code injects into a game process and hooks graphics API entry points. Use it
only in offline/single-player environments that allow ReShade add-ons. Do not use
it with anti-cheat-protected or competitive multiplayer software.

The scope is intentionally modular: this add-on concentrates on native D3D12
DLSS Feature 1 games and the two `SR -> NR` / `NR -> native game SR` orders.
DX11, Vulkan, games without native DLSS SR, and runtime portability for older
GPUs remain separate Bridge/Feeder/ShortFuse concerns rather than duplicate
implementations inside this project.

## Included source

- `src/nr_before_sr.cpp`: ReShade add-on, NGX observation/substitution, HDR proxy
  composition, resource adaptation, overlay, and fail-safe behavior.
- `src/nrchain_nvngx.cpp`: small forwarding bridge used by the experiment.
- `config/nr_before_sr.ini`: conservative default settings.
- `third_party/minhook`: minimal x64 MinHook source subset required by the build.
- `docs/TEST_EVIDENCE.md`: sanitized, non-authoritative runtime observations.
- `docs/BETA_0_3_RELEASE_NOTES.md`: current behavior and safety changes.
- `docs/BETA_0_3_1_RELEASE_NOTES.md`: startup-stability hotfix and release gate.
- `docs/BETA_0_3_2_RELEASE_NOTES.md`: deferred-module compatibility and
  diagnostic changes from the previous candidate.
- `docs/BETA_0_3_3_RELEASE_NOTES.md`: create-time tuning reconstruction, live
  non-HDR R8 admission and Scene Paper-White diagnostics under current testing.
- `docs/BETA_0_3_TEST_MATRIX.md`: release qualification checklist.
- `docs/COMMUNITY_FEEDBACK_BETA03.md`: sanitized mapping from early reports to
  current fixes, intentional bypasses, and remaining evidence gaps.
- `docs/INSTALL.zh-CN.txt` / `docs/INSTALL.en.txt`: short player instructions,
  packaged as `说明.txt` or `Instructions.txt` in full releases. Markdown installation
  documents remain source documentation and are not the packaged instructions.
- `scripts/validate-codec-shader.ps1`: compiles all embedded HLSL entry points
  with the same Shader Model 5.0 compiler contract used at runtime.
- `scripts/Verify-ReleaseManifest.ps1`: optional internal legacy verifier; it is
  retained for reproducibility but is no longer bundled in simple release ZIPs.

## Build prerequisites

The repository does not download or vendor the toolchain or ReShade headers.
Provide both explicitly:

1. An LLVM-MinGW x86_64 toolchain containing
   `x86_64-w64-mingw32-clang.exe` and `x86_64-w64-mingw32-clang++.exe`.
2. A ReShade source checkout containing `include/reshade.hpp` and
   `deps/imgui/imgui.h`.

From PowerShell:

```powershell
./scripts/build.ps1 `
  -ToolchainRoot 'D:\path\to\llvm-mingw' `
  -ReShadeSourceRoot 'D:\path\to\reshade'
```

Alternatively set `DLSS5_LLVM_MINGW_ROOT` and `DLSS5_RESHADE_ROOT`. Build output
is written only to `build/`, which is ignored by Git.

No proprietary runtime is needed to compile the source. Vendor binaries and
their download links are not provided by this repository.

## Local release packaging

The packaging script uses existing local build output. To create small update
archives without copying the large runtime DLLs, use:

```powershell
./scripts/package-beta03.ps1 `
  -ReleaseRoot 'D:\releases\beta0.3.3.5-update' `
  -UpdateOnly
```

For full packages, omit `-UpdateOnly` and provide `-Rtx50Runtime` and
`-Rtx3040Runtime` with the matching local runtime paths. Choose a new destination
for each run. Both package types include plain-text player instructions and the
offline collector pair. Generated archives and binaries stay outside version
control; the repository retains only their source, scripts and documentation.

## Current research status

The two chains have executed successfully in several D3D12 titles, including
contracts with FP16 SceneColor, UE-style `R11G11B10_FLOAT` SceneColor, padded
active regions, low-resolution depth/motion guides, and high-resolution motion
conversion. It also has a contract-gated `R10G10B10A2_UNORM` HDR path that
symmetrically decodes and re-encodes ST.2084 PQ through a controlled FP16
intermediate; packed R10 without the HDR/unity-exposure contract remains a
native-SR bypass. The current candidate also admits a shader-loadable 1x1
exposure-scale texture by sampling its red channel symmetrically on
the GPU; unsupported exposure contracts remain native-SR bypasses. Results are
engine- and runtime-dependent. Important unresolved
areas are documented in [KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md).

The 2026-09-04 tree is a **beta0.3.3 development candidate**, not a universal
release. It compiles cleanly in the maintained local toolchain, all embedded
shader entries compile, and both language builds pass focused LLVM analysis.
The prior beta0.3.2 Chinese binary with SHA256 `4E483F...B1AF1` completed fresh Cyberpunk 2077
RR-off and RR-on runs: the default `NR -> SR` path exceeded 1,800 carrier
evaluations without a logged lifecycle/device failure, while the RR run exceeded
1,800 Feature-13 evaluations with zero Feature 18 work. The gameplay-validated
Chinese r2 binary (`1E717C...7F560`) separately completed an exact-hash Witcher
gameplay run with native SR, both orders, and a Lighting Match ablation; every
active interval exceeded 600 frames without a logged lifecycle/device failure.
beta0.3.3 changes the Feature 18 lifetime when create-time tuning controls are
committed, adds the packed R10 PQ path described above, and admits a live exact
`R8G8B8A8_UNORM` render input when unity exposure plus the current Feature 1
flag or matching current Output prove one bounded value domain. Revision r2
also uses the broadly supported `cs_5_0` codec target, rebuilds a same-handle
FP16/R8 contract, and prevents a failed HDR/DLSS Create with a null handle from
permanently latching RR ownership uncertainty. Failed/null Create attempts no
longer count as successful SR activity for late-module isolation; a genuinely
unknown non-null pre-observer handle remains a conservative process-long bypass.
Late lifecycle detours are published in one queued transaction. Those changes
require fresh Red Desert, Yanyun, Horizon and
Witcher-HDR community logs before they become compatibility claims.
Hogwarts still requires a current-hash visual gate, and the remaining physical
hotkey/order plus three-preset tests are open. The seven-title 600-frame/20-cycle
matrix is reserved for RC.

The project should not claim exact visual equivalence between Mode 1 and Mode 2:
Mode 2 gives the neural model fewer source pixels and then asks the game's SR to
reconstruct the result. Some local face and texture differences are inherent to
the different processing order. A global shift can come either from a broken
color contract or from a broad luminance residual produced by the NR model
itself; beta0.3.3 logs the proxy, neural and composited means separately so those
causes are not conflated.

## Contributing and security

Read [CONTRIBUTING.md](CONTRIBUTING.md) before sharing changes and
[SECURITY.md](SECURITY.md) before reporting a crash or unsafe resource-lifetime
problem. Reports must be sanitized and must never include proprietary binaries.

## Licensing status

No project-wide open-source license has been selected yet. Until the repository
owner confirms contributor rights and adds a `LICENSE`, this source remains
unlicensed and should stay private. See
[LICENSE_RECOMMENDATION.md](LICENSE_RECOMMENDATION.md). Third-party components
retain their own licenses; see [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
