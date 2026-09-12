# Upstream NR reuse inventory / 上游 NR 可复用合同清单

Date: 2026-09-13  
Lab base (read-only): `codex/dx12-compat-multipass-20260912` @ `3c13f69119ddb0a0c1246f0348eb15c8aac64f76`  
This document is an inventory. It does **not** implement Present #257, change LICENSE, or vendor any upstream tree.

Pinned public heads used for this survey:

| Repo | HEAD / pin | License |
|---|---|---|
| [optiscaler/OptiScaler](https://github.com/optiscaler/OptiScaler) | `5ee53e38e6f012d8f6420301063a73e47c114f84` (2026-09-12) | **GPL-3.0 — idea-only** |
| [Dagherbou/OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR) | `973761621353b99bee3dc7d4bb27b117fef2644f` + cited PRs | **GPL-3.0 — idea-only** |
| [clshortfuse/renodx](https://github.com/clshortfuse/renodx) public main | `cd32113a98608e63027d40910cfe296a14dfe228` (2026-09-05) | MIT, **no matching NR addon source** |
| RankFTW `rhi-repo` binary tags | `renodx-dlss5-5.2.1` / `renodx-dlss-SF-0.55` @ `8edcb83` (2026-09-11) | **observe-only** (no SPDX; addon binaries) |
| [SAOG0721/Magpie](https://github.com/SAOG0721/Magpie) Experimental `DLSSNRFilter` | `ac1cc8b0f2efc78323898395cc1336bcbecdc276` | **GPL-3.0 — idea-only** (residual notes only) |

Wiki pin: OptiScaler [Known Issues](https://github.com/optiscaler/OptiScaler/wiki/Known-Issues) last edited 2026-08-14; in-tree `Issues.md` blob `9bba2aa10f03c8244c07a77ef4bcdfccec2084ba`.

**License rule used below:** GPL = copy the *contract idea*, never the source. MIT public RenoDX = portable only where the public tree actually has the NR path (it does not). ShortFuse / Generic NR addons = observe binaries and our existing static notes; do not vendor.

---

## Reuse first / 先复用这 12 条

1. **TYPELESS 必须 typed 才能 Copy/CreateView，但禁止 UNORM 猜测表** → OptiScaler `TranslateTypelessFormats` @ `5ee53e38` + DLSSNR `TypedForCopy` in `DlssNr_Capture.h` on `97376162` → **我们已拒绝猜表**（`nr_resource_semantics.h`）。下一步只补 *Feature 代复用已有视图、无新 CreateView* 的账本恢复（#2 / #248），不要抄他们的 R8→UNORM。
2. **Create 与 Evaluate 不得同列表** → DLSSNR `DlssNr_Dx12.cpp` creation-frame early return + PR [#16](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/16) merge `f6e4738c0a24c7d5a314b2655ed6e4c30142ad40` → 我们已有 Create-first-recording；下一步把 *创建帧也套状态信封* 做成共享合同，而不是再写一套中间件。
3. **导引/曝光 barrier 用到达态，不用 NGX 文档假设态** → 同一 PR #16（`DepthResourceBarrier` / `MVResourceBarrier` / `ExposureResourceBarrier`，未设则 no-op）→ 我们自有资源用 `Transition` 记账；对 *游戏传入* Color/Depth/MV/Exposure 不要再假设 `NON_PIXEL_SHADER_RESOURCE`。
4. **MV 分辨率、子矩形、XY 独立缩放** → DLSSNR PR [#42](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/42) head `fa78de5c23077e3481708245bd6cda819c5f264c` → 我们已读 `MV.Scale.X/Y` 并做工作网格换算；缺的是 `MVLowRes` + 游戏 subrect clamp。**copy-idea。**
5. **同帧 GPU 采样 ExposureTexture，禁止 3 帧 CPU readback** → DLSSNR PR [#12](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/12) merge `ad1120344666978acfd3b13b5af35d69f896d0d3` → 我们已有 typed R8 归一化红通道读取（#228/#229）；下一步是 *Encode/Resolve 内采样 + 1×1 dummy 回退*，不是再扫一遍 UAV。
6. **双尺寸 Evaluate 用分辨率滞后，不要每帧重建 Feature** → DLSSNR issue [#29](https://github.com/Dagherbou/OptiScaler_DLSSNR/issues/29)（MSFS 双 viewport；K 连续命中才信新尺寸）→ 这是 Feature 尺寸合同，**不是 Present #257**。我们已有 `feature_generation` 隔离，缺滞后。**copy-idea。**
7. **双 NR 所有权互斥** → 我们已 latch `renodx-dlss.addon64` HookPoint≠0 与 `renodx-dlss5.addon64` 进程级冲突 → **already-done**。对照 RenoDX `DirectNeuralRenderingHookPoint` 枚举即可，不要再接第二条 Feature 18。
8. **HookPoint / PassCount / Encoding 是阶段合同，不是游戏名单** → SF 0.51 键（见 `docs/RENODX_2026_09_04_COMPARISON.md`）；最新公开二进制 tag 已到 SF 0.55 / Generic 5.2.1 @ `8edcb83`，**无源码** → 我们已有 Before/After/Present 路由与 D11 双次 NR。对照枚举，不要抄 addon。
9. **匹配残差：改量在低网格算一次，再铺回全分辨率代理** → Magpie `PrepareResidual` @ `ac1cc8b0` + DLSSNR PR #12 empty-model gate → D11 已有独立 Feature/匹配增量。**already-done（思路）**；不要把 Magpie 当第二套核。
10. **命令错绑用状态信封，不要猜游戏 PSO** → OptiScaler `RestoreComputeSignature` / `RestoreDescriptorHeaps` @ `5ee53e38` + DLSSNR `ScopedNrStateEnvelope` → 我们 Mode 1 已标明无法通用恢复。**copy-idea：** NR 自己的 bind 必须出列表前还原；不要做成默认 spoof/hook 全家桶。
11. **曝光格式认不出时不要默认 AutoExposure** → OptiScaler wiki Known Issues / `Issues.md` → 我们已把 LDR 的 HDR 专用 ExposureTexture 从否决里拆开，但仍要求 typed 合同。**skip** 把 AutoExposure 当格式修复。
12. **不要做的轮子（整节见 §6）** → spoof-as-default、exe 白名单、强制 Opti 中间件、TYPELESS→UNORM 表、Present #257 实现。

---

## 1. Resource / view / format

TYPELESS、CreateView、generation invalidation

| Capability (what problem it solves) | Upstream where (repo + SHA/PR/issue) | License note | Do we already have something? | Reuse mode | Maps to our issues |
|---|---|---|---|---|---|
| CreateView / Copy 不能携带 TYPELESS；placed footprint 为 TYPELESS 会 `CopyTextureRegion` 失败并丢设备 | OptiScaler_DLSSNR `DlssNr_Capture.h` `TypedForCopy` on `97376162`（注释写明 capture 因此崩过） | GPL idea-only | 我们要求 typed view 才准入；WARP 已覆盖 TYPELESS 物理 + typed UAV | **copy-idea**（“必须 typed”） | #2 #202 #248 |
| 把 TYPELESS **猜成** FLOAT/UNORM/UINT 以便自己建 SRV/UAV | OptiScaler `Shader_Dx12::TranslateTypelessFormats` @ `5ee53e38`：`R8G8B8A8_TYPELESS→UNORM`，`R10G10B10A2_TYPELESS→UINT`，`R16G16B16A16_TYPELESS→FLOAT`。FSR4 `ffxResolveTypelessFormat` 在 `FFXFeature.h` 同 HEAD：`R8_TYPELESS→UNORM` | GPL — **do not copy the table** | `nr_resource_semantics.h`：物理 TYPELESS 不是证据；要 ObservedNativeView 或 UniqueContract；明确拒绝“用已实现 codec 制造唯一性” | **skip** | #2 #248（绝区零 NR=0 正是多种合法解释） |
| 原生 CreateView 观察 + Feature **代** 隔离；旧代 UNORM 不得移植到新 Feature | 上游无对等账本。Opti 直接猜格式，所以没有 generation invalidation | — | Corefix.4–6：ledger 按设备/资源/Feature 代/角色；新 Feature 复用旧描述符且无 CreateView → 格式未知。CPU Clear 可留独立 typed 声明，仍须 current-call gate | **already-done**（观察器）；缺口是 **复用视图无 CreateView** | #2 #202 #203 #248 |
| 代理/原生设备别名下仍能找回同资源 CreateView | 上游无 | — | Corefix.4 `COREFIX4_OBSERVATION_20260910.md`：同生命周期可恢复 CreateView 证据，不升级为准入 | **already-done** | #2 |
| Swapchain RTV 的 TYPELESS/sRGB 对齐（HUD/比较用，不是 NR 颜色合同） | OptiScaler `HC_Dx12.cpp` / `RUI_Dx12.cpp` @ `5ee53e38`：`TranslateTypelessFormats` 匹配 swapchain | GPL idea-only | 我们不走 Opti HUD 比较链 | **skip**（非 NR 合同） | — |
| 大范围 CopyDescriptors 不得全局作废无关格式记录 | 上游无 | — | Corefix.8：超观察上限只失效 API 声明的目标范围 | **already-done** | #2 附属 |

**Maintainer takeaway:** 上游“已解决 TYPELESS”=猜表。我们“已解决”=观察+拒绝猜测。剩下要发明的只有一件：**当前 Feature 代在没有新 CreateView 时，如何合法继承仍在用的 typed 视图**。不要开第二条 UNORM 表 PR。

---

## 2. Guides

depth, MV scale/units, jitter polarity/flags, exposure / preExposure

| Capability (what problem it solves) | Upstream where | License note | Do we already have something? | Reuse mode | Maps to our issues |
|---|---|---|---|---|---|
| `MVLowRes` / DisplayResolution 标错 → 运动模糊过重；应用层可覆盖 flag | OptiScaler wiki Known Issues “Display Resolution Motion Vectors”；`IFeature.cpp` @ `5ee53e38` L61–68：`DisplayResolution` 覆盖 `LowResMV` | GPL idea-only | 我们读 `MV.Scale.X/Y`，无效则 1.0 并换算到工作网格；**没有** DisplayResolution 覆盖旋钮 | **copy-idea**（认 flag，不认游戏名） | #225（jitter 关仍闪，MV 合同仍独立） |
| MV 可能是渲染分辨率或输出分辨率；subrect 可非 (0,0)；XY 必须独立缩放 | DLSSNR PR [#42](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/42) head `fa78de5c`：尊重 `MVLowRes`、转发 depth/MV subrect、clamp 到资源、XY 独立、DX12+VK | GPL idea-only | `nr_before_sr.cpp` 已有 scale×source_to_work；D15 文档写生产键 `DLSSNR.Depth` / `DLSSNR.MVec` | **thin-adapter**：把 PR#42 的 *元数据合同* 接到现有 binding，不抄实现 | 阴影闪（上游 SR+NR 症状；我们 #225/#205 同族） |
| Jittered MV flag 可覆盖 | OptiScaler `IFeature.cpp` L51–58：`JitterCancellation` → `JitteredMV` | GPL idea-only | `NrJitterMode::{Legacy,GameInputPixel}`；corefix.2 默认关、迁移后可开 | **already-done**（我们自己的极性/网格换算）；不要再加 Opti 覆盖 INI | #225 |
| DepthInverted flag 可覆盖 | OptiScaler `IFeature.cpp` L41–48 | GPL idea-only | `DLSSNR.DepthInverted` + Create flags 对齐（dev12）；RR 另有硬件深度→可读视图 | **already-done** | #99 #101 |
| UE 膨胀 MV 与 Output Scaling 不兼容 | OptiScaler wiki “Output Scaling greyed out”：Dilated MVs | GPL observe-only | 我们不做 Opti Output Scaling | **skip** | — |
| 曝光纹理格式不被 upscaler 识别 → 暗部压黑/白屏；上游默认开 AutoExposure | wiki “Exposure Texture”；`Issues.md` `9bba2aa1` | GPL — **idea that format≠value**；**skip AutoExposure-as-fix** | typed R8_UNORM 1×1 归一化红通道；R8 TYPELESS/UINT/SNORM 仍拒绝；LDR 不因 HDR 专用 ExposureTexture 否决 | **already-done**（typed 读）；**skip**（AutoExposure 默认） | #228 #229 #68 |
| 同帧 GPU 采样 ExposureTexture 作 paper white；1×1 dummy SRV；heldE 填洞 | DLSSNR PR [#12](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/12) merge `ad112034`：替换 3 帧 CPU readback；`PaperWhite()` in-pass | GPL idea-only | 我们有 GPU 读回路径与 `gpuRawExposure` F8 字段；仍可能落后一拍（见 D12 HDR 预曝光） | **copy-idea**（in-pass，不扫 UAV） | #228；对照 DLSSNR [#41](https://github.com/Dagherbou/OptiScaler_DLSSNR/issues/41) |
| 扫描游戏 UAV 猜 ExposureTexture（1×1 R16F 等） | DLSSNR `ExposureScan`；#41 KCD2：大量 near-miss（fmt 28/61/10…），后 adopt 1×1 R16_FLOAT 仍 “Reading the exposure…” | GPL — **observe-only** | 我们只绑定游戏 NGX 给出的 `ExposureTexture`，不扫资源 | **skip**（扫描器） | #228 |
| `DLSS.Pre.Exposure` 与 ExposureTexture / AutoExposure flag 必须一起解释 | #41 日志：`Pre.Exposure 1.000000, ExposureTexture supplied, auto-exposure flag set` 仍读不成 | observe-only | 我们保留原生参数；非 unity 标量拒绝有界 R8；两个 codec pass 对称读当前资源 | **already-done**（合同拆分） | #228 #229 |
| RR / 线性深度：只转换已知硬件深度，不把结果当米制 view-Z | Opti 主要做 SR/FSR 深度 invert/scale shader（`depth_invert` / `depth_scale` @ `5ee53e38`） | GPL idea-only | `nr_rr_depth_adapter.inl`：已知硬件深度→可读；线性/未知保留原生 RR；D13 补 `R32G8X24_TYPELESS` 读视图 | **already-done** | #99 #101 |
| Magpie NR 路线选 **零深度**；可选 MV 来自光流 | Magpie `DLSSNRFilter.cpp` + `FrameGuidanceTypes.h` @ `ac1cc8b0` | GPL idea-only | 我们能送引擎 depth/MV；合成仍无深度槽。不要学“默认零深度” | **skip**（零深度默认） | — |

---

## 3. Command / queue / lifetime

barriers, Create vs Evaluate lists, fence, Feature recreate

| Capability (what problem it solves) | Upstream where | License note | Do we already have something? | Reuse mode | Maps to our issues |
|---|---|---|---|---|---|
| Create 与 Evaluate 同列表会挂 GPU；创建走游戏 submit，下帧才 Evaluate | DLSSNR `DlssNr_Dx12.cpp` ~L1795–1798 on `97376162`；PR #16 说明 vkd3d 上更致命 | GPL idea-only | Create-first-recording（#205 首轮无 Reset）；Feature 代隔离 | **thin-adapter**：创建帧也必须进状态信封（PR#16 后半） | #205 #223 |
| 创建帧曾跑在信封外，NGX heap/root/PSO 留在游戏列表上 | PR [#16](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/16) merge `f6e4738c`：`ScopedNrStateEnvelope` 包住 create；`CanRestoreRootSignature` | GPL idea-only | Mode 1 已知无法通用查询/恢复游戏 PSO（`KNOWN_LIMITATIONS.md`） | **copy-idea**（只还原 *我们* 绑过的） | #205 |
| 导引从假设的 `NON_PIXEL_SHADER_RESOURCE` 过渡会 debug-layer + 丢设备；typed 导引曾完全无 barrier | PR #16；对照 `eb86c532` 对 output 的同类修复（fork 史） | GPL idea-only | 自有 RR depth / 合成面有 `Transition` 记账；**游戏传入** guides 未用到达态键 | **copy-idea** | #2 #205 |
| UE DLSS 插件常给错 Color 状态；表现为底边彩带/右上黑块；用到达态覆盖 | OptiScaler wiki “Resource Barriers”；`ColorResourceBarrier=4` (RENDER_TARGET)；FSR/XeSS Dx12 同键 | GPL idea-only | 我们不提供玩家 barrier INI | **copy-idea**（到达态，不是再做一个 UE 白名单） | — |
| 曝光 SRV 在 Vulkan 仍要 shader-readable layout；D3D12“读不改 state”不成立 | PR #16 + `FSR2Feature_Dx12.cpp:164` 对照 | GPL idea-only | 我们 D3D12-only；若将来跨 API 再看 | **skip**（当前 API） | — |
| 双分辨率交错 Evaluate → 每帧 `resolutionChanged` 重建 Feature → VRAM 爆炸 | issue [#29](https://github.com/Dagherbou/OptiScaler_DLSSNR/issues/29) on `433cc11`/`9737616`：K=30 连续才信新尺寸；另一尺寸 skip | GPL idea-only | 有 generation，无滞后；双层 NR 是独立 Feature，不是双 viewport SR | **copy-idea**（按 *输出尺寸身份* 滞后，不按游戏名） | 不是 #257 |
| Tuning 变立即重建；尺寸变要滞后 | #29 提案明确区分 | GPL idea-only | 我们 tuning 变走 Reset/新 Feature 代 | **already-done**（tuning）；**copy-idea**（尺寸） | #223 |
| 精确 fence 等已提交工作；超时不瞎重试 | 上游用游戏 submit + 下帧 Evaluate，无我们对等的 50ms 预算 | — | queue.3 / corefix.2：精确 fence、50ms、未知队列准入 | **already-done** | #205 |
| RestoreComputeSignature / ExtendedStateRestore / RestoreDescriptorHeaps | OptiScaler `D3D12_Hooks.cpp` @ `5ee53e38`；Config 默认 false | GPL idea-only | 我们不装全局 D3D12 hook 中间件 | **copy-idea**（局部信封）；**skip**（默认全局 hook） | #205 |
| 首轮 Create 后无 Reset 仍可观察录制 | 上游无对等 | — | `DS1_FIRST_RECORDING_20260909.md` | **already-done** | #205 |

---

## 4. Placement / hooks

pre / post / Present, ownership, dual-NR conflict

| Capability (what problem it solves) | Upstream where | License note | Do we already have something? | Reuse mode | Maps to our issues |
|---|---|---|---|---|---|
| NR 挂在哪个颜色阶段：Off / Auto / Upscaled / Hudless / Present | RenoDX SF `DirectNeuralRenderingHookPoint` 0–4；静态证据见 `docs/RENODX_2026_09_04_COMPARISON.md`（SF 0.51）。公开源码仍无对应 addon（renodx main `cd32113a`） | **observe-only**（二进制+我们已有反汇编笔记） | `nr_processing::Start::{Before,After,Present}` + 失败后撤 | **already-done**（阶段枚举）；Present **实现**属 #257，本清单不写步骤 | #257（owner）；#205 |
| Present 源身份：队列、提交、FG 已消费、原生生产者 | 上游 Opti 的 Present 是 FG/HUD，不是 NR 源所有权 | — | `nr_source_frame.hpp` `PresentDecision` / reject flags；D15 要求实际 Present conversion/copyback 另验 | **already-done**（决策面）；**skip 实现**（#257） | #257 #205 |
| Generic RenoDX 与 ShortFuse 都是完整 Feature 18 所有者；不能双开 | RankFTW 发布说明 + Feeder README：`renodx-dlss` *替换* feeder；`renodx-dlss5` *与* feeder 二选一 | observe-only | `nr_before_sr.cpp`：`renodx-dlss.addon64` HookPoint≠0 或 Enabled；`renodx-dlss5.addon64` 进程 latch 后 fail-closed | **already-done** | #107 #223 |
| HookPoint=0 即 Off；`Enabled` 与 HookPoint 可能不一致 | SF 0.51：v0.3 只选 HookPoint 仍会跑 NR | observe-only | 我们把 *任一信号* 当 ownsNR | **already-done** | #107 |
| Encoding / UI correction 是全局键，强度/PassCount/AutoMask 在预设节 | SF `[RENODX-DLSS]` vs `[RENODX-DLSS-preset1]` | observe-only | 我们自己的 INI 节；对照时不要混全局/预设 | **already-done**（对照笔记） | — |
| Opti 作为强制中间件拦截全部 upscaler | OptiScaler README：Inputs → Opti → Outputs | GPL — **skip as core** | `KNOWN_LIMITATIONS.md`：carrier 必须是可观察的原生 Feature 1；Opti 可能藏/换该调用，不支持 | **skip** | — |
| RR 活句柄排除额外 Feature 18 | 上游 PR [#36](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/36) head `1366c6fa`：RR 后另开 NR（实验） | GPL idea-only | 我们是 **coexistence by exclusion**：活 Feature 13 旁路 companion Feature 1 | **already-done**（排除）；**skip**（在 RR 输出上再挂 NR，除非另批授权） | #99 |
| 无 DLSS 游戏用 Feeder/采集伪造请求 | Feeder / Magpie / ShortFuse DX9 | 各异 | 我们 D3D12 + 可观察 Feature 1 only | **skip** | — |

---

## 5. Multipass / residual / color compose

PassCount, PrepareResidual lessons

| Capability (what problem it solves) | Upstream where | License note | Do we already have something? | Reuse mode | Maps to our issues |
|---|---|---|---|---|---|
| 多次 NR：每 pass **独立 Feature/历史**，创建比 Evaluate **早一帧**；用残差链而不是把上一次全图再喂进去 | DLSSNR PR [#23](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/23) head `5a34e8d1`（open；基于 `21274132`）。`0f79b9dd` 曾因同列表 create+eval 撤回 | GPL idea-only | D11：独立 NR2 Feature/参数/历史；第二层等自己的 fence；不透明失败隔离整表 | **already-done**（双次）；五次链 **skip** 除非授权 | D11 线 |
| 用 **present/swapchain 帧** 量“早一帧”，不要用 Dispatch 计数（一帧两次 Evaluate 会重建同列表 hazard） | PR #23 正文 | GPL idea-only | 我们 source-frame 身份不是 FPS/Present 计数；双层不共享创建列表 | **copy-idea**（身份用 source/present，不用 dispatch） | #257 只提供身份，不在此实现 Present |
| 后 pass 失败降 ceiling，不毁 session；按 *上次真实创建代价* 查剩余 VRAM | PR #23 | GPL idea-only | 第二层失败停止后续合成/SR；有 256MiB/slot 预算 | **already-done**（失败隔离）；VRAM 实测代价 **copy-idea** | — |
| Pre-SR NR + 每 pass 控件 + RR 后置选项；padded/offset 区域不安全则回退 post-SR | PR [#36](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/36) head `1366c6fa` / merge-ref `418cbc55` | GPL idea-only | 我们本业就是 NR→SR；有界 Evaluate 可回退后置（#190） | **already-done**（pre-SR）；padded fallback **thin-adapter** | #190 |
| PassCount 1–10（SF）/ `Passes`（Opti NR） | SF 键 `DirectNeuralRenderingPassCount`；DLSSNR INI `Passes` | observe-only / GPL idea | D11 `nr_dual::Settings::passes` 默认 1，第二层 opt-in 50% | **already-done**（有界 1–2）；不要跟 10 | — |
| 匹配残差：`edit = model − proxy`；strength 0 必须回到 keep；空模型要 gate 以免高通泄漏 | PR #12 merge `ad112034` | GPL idea-only | D11：低分有符号残差 + FP32 匹配增量；100% 直用 P2 | **already-done** | D11 |
| 全分辨率代理从已编码 `colorCopy` 解，避免 SoftKnee 漂移 | PR #12 | GPL idea-only | 我们自有 FP16 快照/代理曲线（legacy/hybrid/reversible） | **copy-idea**（代理与编码同源） | — |
| `PrepareResidual`：所有残差控制在 **低分辨率像素** 做一次，再 Catmull 上采样铺回原图；保留 alpha | Magpie `DLSSNRFilter.cpp` L397–508 @ `ac1cc8b0`（`PrepareResidual` / horizontal / vertical composite） | GPL idea-only；D11 已用自有实现，未复制源码 | D11 通用 RGB/HSL 分层默认关；Catmull 编辑恢复独立切片 | **already-done**（思路）；**skip** 当第二核 | 见既有 `MAGPIE_*` 文 |
| 导引必须同帧、同子矩形、同网格 | Magpie `FrameGuidanceTypes.h` L64–134 @ `ac1cc8b0` | GPL idea-only | `nr_guide_contract.h` + source-frame | **already-done** | — |
| Intensity>1 在无 ControlMask 时不改变 310.8 网络控制上传 | 我们自己的运行库只读分析（`RENODX_2026_09_04_COMPARISON.md`） | observe-only | 不要把 SF 滑条 0–1 或 Generic 0–2 当成模型已增强 | **already-done**（不要发明 Intensity=2 修复） | — |
| AutoMask / Skin 是模型控件，不是空间人脸 mask | SF `DirectNeuralRenderingAutoMask`；Magpie 传 `UseAutoMask` 但不把脸/UI mask 给残差合成 | observe-only / GPL idea | 我们保留模型 AutoMask；不承诺额外增强已与人脸隔离 | **already-done** | #162（暂停） |

---

## 6. Things to SKIP

明确不要复用 / 不要做成默认

| Capability (what problem it solves) | Upstream where | License note | Do we already have something? | Reuse mode | Maps to our issues |
|---|---|---|---|---|---|
| 默认 DXGI/NVAPI spoof 成 NVIDIA 以骗出 DLSS 输入 | OptiScaler `Spoofing.md` + wiki “Intel Arc spoofing” / “Graphical Corruption” | GPL | 我们不 spoof | **skip** | — |
| 按 exe / 游戏名单特判 | Opti 兼容列表、OptiPatcher 支持表、UE ini 游戏例 | GPL / 社区名单 | 机制合同，不用游戏分支 | **skip** | AGENTS.md |
| 强制 Opti 中间件（改名 dxgi.dll、替换全部 upscaler） | OptiScaler README | GPL | carrier = 可观察原生 Feature 1 | **skip** | `KNOWN_LIMITATIONS.md` |
| TYPELESS→UNORM/FLOAT **猜测表** 作为准入 | `TranslateTypelessFormats` / `ffxResolveTypelessFormat` / `TypedForCopy` 的 *默认映射* | GPL | UniqueContract 仅当 *唯一* 合法解释 | **skip** | #2 #248 |
| AutoExposure=true 当作“曝光格式未知”的修复 | wiki Exposure Texture | GPL | typed 读或拒绝 | **skip** | #228 |
| 扫描进程 UAV 猜 ExposureTexture | DLSSNR `ExposureScan`；#41 失败记录 | GPL | 只信 NGX 参数 | **skip** | #228 |
| 把 Magpie Experimental 当第二套 NR 核 / 默认零深度 | `ac1cc8b0` | GPL | 残差/分层已借鉴 | **skip**（核）；残差见 §5 | — |
| 实现 Present #257（owner 自有） | — | — | 决策/拒绝枚举已在 `nr_source_frame.hpp` | **skip** | #257 |
| 与 Generic/SF 同时提交 Feature 18 | rhi-repo 发布物 | observe-only | 已 latch | **skip**（双开） | #107 |
| Intensity / HintPreset / 游戏白名单当画质旋钮 | 我们 09-04 对照 | observe-only | 310.8 单 preset | **skip** | — |
| 本清单期间再开一条 TYPELESS 猜表代码 PR | — | — | 观察器已在；缺的是 generation 复用，不是小补丁 | **skip this run** | #2 |

---

## Pin index / 引用索引

| ID | What | SHA / URL |
|---|---|---|
| OptiScaler HEAD | survey snapshot | `5ee53e38e6f012d8f6420301063a73e47c114f84` |
| OptiScaler Issues.md | wiki-equivalent in tree | blob `9bba2aa10f03c8244c07a77ef4bcdfccec2084ba` |
| DLSSNR HEAD | v0.2.0-patch1 line | `973761621353b99bee3dc7d4bb27b117fef2644f` |
| DLSSNR #12 | in-pass exposure + matched residual | merge `ad1120344666978acfd3b13b5af35d69f896d0d3` |
| DLSSNR #16 | create-frame envelope + barrier keys | merge `f6e4738c0a24c7d5a314b2655ed6e4c30142ad40` |
| DLSSNR #23 | 5-pass chain, present-based settle | head `5a34e8d19d9468bc908a6f0b9167e74c2cb95466` (open) |
| DLSSNR #29 | resolution hysteresis | issue on `433cc11` / `9737616` |
| DLSSNR #36 | pre-SR + per-pass + RR option | head `1366c6fabd536193e3b237759ca97821ff2fe36d` |
| DLSSNR #41 | exposure scan failure (KCD2) | open issue |
| DLSSNR #42 | MVLowRes / subrect / XY scale | head `fa78de5c23077e3481708245bd6cda819c5f264c` |
| Magpie NR filter | PrepareResidual | `ac1cc8b0f2efc78323898395cc1336bcbecdc276` |
| RenoDX public | no NR addon source | `cd32113a98608e63027d40910cfe296a14dfe228` |
| rhi-repo addons | latest binaries only | `8edcb83` tags `renodx-dlss5-5.2.1`, `renodx-dlss-SF-0.55` |
| Lab SF/Generic keys | HookPoint/PassCount/AutoMask | `docs/RENODX_2026_09_04_COMPARISON.md` (SF 0.51 / Generic 4.70) |
| Lab semantics | TYPELESS resolver | `src/nr_resource_semantics.h` |

Evidence boundary: this survey is public-source / already-committed lab docs / public PR text. It is not NVIDIA, in-game, or hosted-CI validation. Private logs and addon binaries were not copied into this tree.

---

## What we must still invent

Shared contracts only — not per-game:

1. **Current-generation typed view without a new CreateView** (cached descriptor reuse after Feature recreate). Upstream does not have this; they guess UNORM instead.
2. **Arrival-state barriers for game-owned Color/Depth/MV/Exposure** (keys or observed state; unset = no-op).
3. **MVLowRes + subrect + independent XY** on the existing `MV.Scale` binding.
4. **In-pass exposure sample** (no UAV scan, no AutoExposure default).
5. **Output-size hysteresis** for interleaved Feature 1 sizes (issue #29 idea; not Present hysteresis).
6. **Create-frame state envelope** so NGX binds do not leak (Mode 1 still cannot restore arbitrary game PSO).

Present #257 remains owner-owned and is not started from this inventory.
