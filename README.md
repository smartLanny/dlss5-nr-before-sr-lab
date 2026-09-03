# DLSS5 NR-Before-SR Lab

Experimental, source-only ReShade D3D12 add-on for studying two neural-rendering
orders around a game's ordinary DLSS Super Resolution call:

- Mode 1: game DLSS SR, then output-resolution neural rendering (`SR -> NR`).
- Mode 2: render-resolution neural rendering at 1:1, then the game's original
  DLSS SR (`NR -> SR`). Mode 2 is the beta0.3.1 default performance path; Mode 1
  remains the complete full-resolution quality/reference path.

This is a research project, not an NVIDIA product and not an official joint
DLSS NR/SR implementation. NVIDIA, DLSS, GeForce, and related names are
trademarks of their respective owners.

Project author: **@野生的装机宅 Bilibili**. The only official release location
is https://space.bilibili.com/941799. Distributed archives include a signed
manifest and offline verifier; matching filenames alone do not prove origin.

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
- `docs/BETA_0_3_TEST_MATRIX.md`: release qualification checklist.
- `docs/COMMUNITY_FEEDBACK_BETA03.md`: sanitized mapping from early reports to
  current fixes, intentional bypasses, and remaining evidence gaps.
- `docs/INSTALL.zh-CN.md` / `docs/INSTALL.en.md`: quick deployment, conflict
  removal, runtime placement, and bug-report instructions.
- `scripts/validate-codec-shader.ps1`: compiles all embedded HLSL entry points
  with the same Shader Model 5.1 compiler contract used at runtime.
- `scripts/Verify-ReleaseManifest.ps1`: validates the project ECDSA signature
  and every package file hash without needing the private signing key.

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

No proprietary runtime is needed to compile the source. This repository does not
provide runtime installation instructions or download links for vendor binaries.

## Current research status

The two chains have executed successfully in several D3D12 titles, including
contracts with FP16 SceneColor, UE-style `R11G11B10_FLOAT` SceneColor, padded
active regions, low-resolution depth/motion guides, and high-resolution motion
conversion. The current candidate also admits a shader-loadable 1x1
exposure-scale texture by sampling its red channel symmetrically on
the GPU; unsupported exposure contracts remain native-SR bypasses. Results are
engine- and runtime-dependent. Important unresolved
areas are documented in [KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md).

The 2026-09-03 tree is a **beta0.3.1 startup-stability candidate**, not a universal
release. It compiles cleanly in the maintained local toolchain and passes a
focused static analysis. The exact rebuilt Chinese binary passed a Cyberpunk
2077 direct-start smoke run and sustained the default `NR -> SR` path beyond
2,400 carrier evaluations without a logged lifecycle/device failure. Runtime
observations for the other processing order, clean Enabled cycles, and an RR
interval with zero Feature 18 still come from the immediately preceding graphics
candidate. Hogwarts' two orders and three live DLSS contracts plus the Witcher
mixed-guide post-SR path are likewise prior-candidate evidence. The current
binary still needs the remaining three-title gate, physical F6, RR-on, mode and
preset switching. The seven-title 600-frame/20-cycle matrix is reserved for RC.

The project should not claim exact visual equivalence between Mode 1 and Mode 2:
Mode 2 gives the neural model fewer source pixels and then asks the game's SR to
reconstruct the result. Global lighting drift is considered a correctable color
contract problem; some local face and texture differences are inherent to the
different processing order.

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
