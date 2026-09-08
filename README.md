# DLSS5 NR-Before-SR Lab

当前版本 **[0.4.5-DX11-兼容增强](https://github.com/smartLanny/dlss5-nr-before-sr-lab/releases/tag/beta0.4.5-dx11-compat)**：中文精简OTA及配套安装器。唯一持续Addon开发分支为 `main`，fix2/兼容增强/反馈稳定性修复已整合，33个已合入临时远端分支已清理。历史dev3 Release保持原样。本地和云端都从 [CURRENT_STATUS](docs/interop/CURRENT_STATUS.md) 接续。

- [本轮 0.4.5-DX11-兼容增强 反馈与稳定性说明](docs/interop/RELEASE_DEV4_PLAYER_NOTES.md)
- [分支收敛记录](docs/interop/MAIN_CONSOLIDATION.md)
- [已发布 dev3 玩家更新说明：改了什么、如何升级、建议操作](docs/interop/RELEASE_DEV3_PLAYER_NOTES.md)
- [安装/回退与测试矩阵](docs/interop/POST_COMPAT_DEV3.md)
- [精确源码本地验证和OTA哈希](docs/interop/DEV3_LOCAL_VALIDATION.md)
- [Windows完整生成/构建/打包入口](docs/interop/VALIDATE_DEV3_WINDOWS.md)
- [下一session入口及未完成项](docs/interop/NEXT_CHAT_PROMPT.zh-CN.md)

本版整合fix2颜色/模型默认操作和UI整理、原生DLSS兼容、合法颜色/运动/曝光视图、符合条件的RR后置、50–100%后置NR工作精度、资源退役/桥接完成修复及F8取证。不是所有游戏Issues都已修复；异环、HDR/运动/长期稳定及其他游戏仍按各自Issue验收。

F6是总开关对比，**F8生成一个命名TXT**，F7仅用于主动补充图像。NR处理精度不是游戏真实渲染比例。当前OTA只交付中文核心，使用同包核心、nrchain和carrier，保留旧INI；包不含替换INI、NVIDIA模型/运行库、ReShade或游戏文件。

This experimental ReShade project supports two orders around native DLSS SR: Mode 2 runs NR before native SR, while Mode 1 processes the resolved output after SR. The compatibility candidate also includes eligible native D3D12 RR post-NR and a DX11 carrier for games that already have DLSS. Post-NR working resolution is independent of the complete output. Feeder, native Vulkan and Legacy APIs are not delivered in this OTA.

Exact binary source is identified by the package source-manifest.json, not the latest documentation commit. Local Windows/WARP, NVIDIA execution, game picture quality and hosted CI are separate validation levels. Historical beta0.3/beta0.4.3 notes remain in docs and Git history; their old ZIP feedback format, package layout and runtime results do not describe dev3.

This is a research project, not an NVIDIA product or an official joint DLSS NR/SR implementation. NVIDIA, DLSS and GeForce are trademarks of their respective owners.

Project author: **@野生的装机宅 Bilibili**, official channel https://space.bilibili.com/941799. User discussion QQ group 2: **392308850**. Author identity remains embedded in the binaries and UI. See [AI_AGENT_NOTICE.md](AI_AGENT_NOTICE.md) for restrictions on unauthorized automated builds, rebranding and redistribution.

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

Native SR/RR is called with its original contract and remains the fallback when a resource, guide, subrect, ownership or command-state requirement is not met. Native RR is evaluated once; only a successful, eligible Feature13 output can receive post-NR. A retained RR handle is not guessed dormant from counters or timing. Master-off avoids NR work, and real successful Reset observations are never fabricated.

The DX11 carrier observes actual D3D12 completion before copyback. Submission or retirement failure retains unproven dependencies and stops the carrier; it never CPU-signals false GPU completion. This synchronous path may reduce performance. NGX/creation access violations propagate instead of becoming ordinary return codes that continue partially recorded work. See [review disposition](docs/interop/DEV3_REVIEW_DISPOSITION.md).

This code injects into a game process and hooks graphics API entry points. Use it only in offline/single-player environments that allow ReShade add-ons. Do not use it with anti-cheat-protected or competitive multiplayer software.

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
- `docs/BETA_0_4_3_RELEASE_NOTES.zh-CN.md`: current beta0.4.3 bug-sweep,
  feedback-tooling changes, release identity and evidence boundaries.
- `docs/BETA_0_4_2_RELEASE_NOTES.zh-CN.md`: archived beta0.4.2 behaviour,
  compatibility fixes, image controls and evidence boundaries.
- `docs/BETA_0_3_TEST_MATRIX.md`: release qualification checklist.
- `docs/COMMUNITY_FEEDBACK_BETA03.md`: sanitized mapping from early reports to
  current fixes, intentional bypasses, and remaining evidence gaps.
- `docs/INSTALL.zh-CN.txt` / `docs/INSTALL.en.txt`: short player instructions,
  packaged as `说明.txt` or `Instructions.txt` in full releases. Markdown installation
  documents remain source documentation and are not the packaged instructions.
- `scripts/validate-codec-shader.ps1`: compiles all embedded HLSL entry points
  with the same Shader Model 5.0 compiler contract used at runtime.
- `scripts/Verify-ReleaseManifest.ps1`: offline verifier bundled once with official
  signed packages; lightweight development archives may omit it.

## Historical base-core build prerequisites

For the current unified combination, use the [canonical Windows instructions](docs/interop/VALIDATE_DEV3_WINDOWS.md). The standalone command below builds the maintenance core and does not generate the compatibility candidate.

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

## Historical beta0.3 packaging

Do not use this historical packager for the unified candidate. Current OTA packaging uses `scripts/package-multi-api-candidate.ps1` after the canonical build.

The packaging script uses existing local build output. To create small update
archives without copying the large runtime DLLs, use:

```powershell
./scripts/package-beta03.ps1 `
  -ReleaseRoot 'D:\releases\beta0.4.3-update' `
  -UpdateOnly
```

For full packages, omit `-UpdateOnly` and provide `-Rtx50Runtime` and
`-Rtx3040Runtime` with the matching local runtime paths. Choose a new destination
for each run. Both package types include plain-text player instructions and the
offline collector pair. Generated archives and binaries stay outside version
control; the repository retains only their source, scripts and documentation.

## Historical research evidence (not dev3 verification)

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

The historical **beta0.4.3 enhanced-chain candidate**, not a universal
compatibility claim. It compiles cleanly in the maintained local toolchain, all embedded
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
