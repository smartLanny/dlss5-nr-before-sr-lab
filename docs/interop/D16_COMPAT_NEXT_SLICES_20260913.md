# D16 / #164 下一刀（2026-09-13）

维护者可只读本节拍板。下文是对 D16 tip 源码与已开 PR 的核对，不是新实现。

## HEAD

- 继续线：`codex/dx12-compat-multipass-20260912`
- Tip：`3c13f69119ddb0a0c1246f0348eb15c8aac64f76`（本地 = 远端）
- 这不是落后 tip。更前的提交都是**独立候选**，未合回本线：
  - [PR #257](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/257) `92179dd`：Present 有界重探（+1 commit，base 即本 tip）
  - [PR #258](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/258) / [#259](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/259)：未接线 shader 实验
  - [PR #256](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/256)：导出到 `next/0.5`，不是 D16 合并 tip

Issues API 对本集成不可读（#164/#190/#2 等 REST 403）。编号按仓库文档、PR 交叉引用与源码合同填写。

## 路径图（现状）

| 路径 | D16 tip 实际位置 | 角色 |
| --- | --- | --- |
| Native D3D12 DLSS Feature1 | `ProcessEvaluate` + `nr_processing::Session` Before/After/Present | **自己的 NR Core** |
| Native D3D11 DLSS | 继承的 DX11 carrier + 部署诊断 | 既有 transport，本 tip 无新 adapter |
| NIGos bridge | 固定 `28aed409` / DX11 历史 pin | **transport / 观察**，不是 NR 后端 |
| Present 兼容车道 | `nr_compat_present_*` + `CanPresent` + SourceFrame claims | 可选第三起点；无导引则 color-only |
| Feeder / 外部 Provider | tip 无生产 Feeder 树；#256 在 `next/0.5` | 同帧 Color/Depth/MV → 同一 Core |
| FSR / XeSS | 无生产 adapter；[#174](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/174) 是已关闭的 FFX 3.1→DLSS SR 隔离样本 | 不得变成第二套 Core |
| Reno / Opti | 进程级「另一 NR 所有者」冲突检测；文档当合同语料 | **可选对照 / 每标题桥**，禁止强制中间件 |
| Magpie | 画质研究（分层/选区/方向亮度），零深度配方 | 不是兼容层 |

#190 有界 Evaluate 前置→后置回退已在历史 dev12/D15 落地。D16 另保留起点 floor 与当前 attempt。  
**仍缺：** `Hold`/`Adopted` 一旦把 `floor_=Present`，`BeginSource` 永远从 Present 起跳——自动回退不会回到 Before/After。这就是 #257 要切的合同。

## 下一刀（只排 3）

### 1. #257 Present 有界重探 — **现在就审这一刀**

- **扩覆盖：** 已能走原生 Feature1 的游戏，在加载/过场短暂 Adopt Present 后卡死兼容车道（鬼武者类报告）。解开后整条 Native 线重新可用，不必新 backend。
- **已有：** Draft PR #257，Linux GCC/Clang+ASan 11 场景 + 生产 `CarrierScope` 宿主测试。未做 Windows/WARP/游戏。
- **风险：** 32/256 是重试策略常数，不是 GPU 完成证明；健康连续 Adopt 不可被周期打断；手选 Present 必须保持。
- **不要：** 把异环 `submitted=1 / source=content=exactWrite=0` 当成同一 bug；不要用 PresentCount 伪造源身份。

### 2. #190 源 / Present 血统 — **#257 之后的证据刀**

- **扩覆盖：** 异环 / 2077 等 Present 准入仍停在「已提交、无 application 源/exactWrite」。修好血统，Present 车道才对更多成品画面成立。
- **已有：** `CanPresent` 拒绝正确；F8 已拆 requested/retained/attempt 与 last-CanPresent。缺的是**一次**原生 SR→可追踪 copy→Present，再插入已知全屏后处理看谁切断 lineage。
- **风险：** 把最新 SR 源贴到任意 backbuffer、去掉 `nativeProducer` 守卫、或把 queue 相等当源证明，会处理 FG/HUD 帧。
- **不要：** 为单个 EXE 开白名单；不要在证据不足时放宽 `PresentPromotionEligible`。

### 3. #2 TYPELESS 唯一合同 — **仍走现有 Native resolver**

- **扩覆盖：** 绝区零等 Output/Color 为 TYPELESS、尚无当前代原生视图时 NR=0。resolver 已拒绝「猜 UNORM」；扩的是**当前调用**的 CreateView/Clear 观察，不是新格式表。
- **已有：** `nr_resource_semantics` + Corefix 观察账本。#248/#202 同机制。
- **风险：** 家族里多个合法 typed 视图时必须保持 Ambiguous，不能按游戏猜。
- **不要：** TYPELESS→UNORM 捷径；不要把 codec 已实现当成唯一性。

## 其余候选（现在不要做）

| 候选 | 为何不排进前 3 |
| --- | --- |
| FSR 3.1 直连 provider | #174 已有隔离 SR 样本。扩的是**无 DLSS** 标题，不修现有 Native 失败。接入是新竖切，不是小合同刀。 |
| #254 / #226 鸣潮 RR 当前投影 | `ResolveRrLinearDepth` 已按**本次** ViewToClip 失败关闭。缺矩阵就 bypass 会送错深度。#258 是未接线 shader。 |
| #239 / #250 AMDOF 合成 | 预览未接 NR。可作无导引时的可选 MV，不能当第一覆盖刀。 |
| Magpie / XeSS 当兼容层 | 见下。会变成第二 Core 或藏起 Feature1。 |
| 把 Opti/Reno/AIO 接进 Native | 违反 #164：它们是语料与可选桥，且已被标成另一 NR 所有者。 |
| 把 NIGos 当 NR 后端 | 它已是桥。不要换算法所有者。 |

## Magpie / XeSS 会不会「不变成第二 Core」地帮忙？

**配方有用，整树没用。**

- 已有配方：外部时域输入 → 标准 SourceFrame（Color/Depth/MV/jitter/Reset）→ **自己的** NR Core。#256 / 历史 Feeder 走这条，不是再嵌一套 NR。
- Magpie 固定研究结论：它的 NR 路线选**零深度**；这里已有引擎 Depth/MV。把它当 multi-backend 中间件 = 再养一个所有者。只借选区/残差思想，已在 D 线画质研究里做。
- XeSS / Opti 转换层若替换或藏起 Feature1，`KNOWN_LIMITATIONS` 已写明不支持。不要 vendor 大树上游。
- GPL 评估可以；本 tip **不改 LICENSE、不进大树**。

## 本 PR 做了什么

只加本交接页，并在 `CURRENT_STATUS.md` 顶部加指针。无生产代码、无 LICENSE、无 vendor。  
实现刀继续审 #257；不要另开平行 Present PR。
