# Null-guard：有限 blend 值准入（2026-09-07 接续 #154）

当前接续基线为 `main@db0577c565d0a81a6f5c010b35698e078adf1205`，保留原 PR #154 的 `7eed541c5aa19503a2613cad8ebc391221486de4` 祖先。本单修正 command-state 准入并补上生成器接线；**不是宣布异环 RR、法环或沉没之城2后置已经游戏验收通过**。main、已发布 Tag、OTA 均未改动。

## 已读取的独立证据

- **#99 树海 / multiapi-dev3**：本次确实通过 ROG 读取其已归档原始 `260907-htgame-beta0.4.5-multiapi-dev3-175401765-3364-1.txt`，不是拿另一人的 #101 代验。`start.commandState.blendObserved=1`、`blend=-0,-1,-2,-3`、`dynamic=0x8`、`freshReset=1`、`tainted=0`、`hasPso=0`、`reason=null-guard-graphics-state-used`。本 scope 成功与提交均为 0；runtime 已 initialized。原始文件仍在私有反馈工作区，不为本 PR 复制原始日志。
- 同一 #99 报告的 RR attempts=4191、recordings=0，但 `recentAttempt=0`、attemptAgeMs=39203，不能把累计 RR 计数说成当前这次 mode=1 快照正在执行 RR，更不能证明修复 blend 后 RR 链路必然成功。#15 / #150 的同次边界验收仍需要。
- **#64 NeroEggs / 已发布 dx11-compat**：`bug-inbox/issue-64/20260907-sheet-30n66b-neroeggs-elden-dx11-compat/` 的原 F8 97–127 行独立给出相同 `blend=-0,-1,-2,-3`、`dynamic=0x8`、covered/freshReset、无 taint/PSO。KEY 记录 start/end scope 成功=0。此材料不再归入早期 fix1 待复测桶。
- **#38**：20260907 fix1 后置已有同样 dynamic=0x8 形态，但旧包没有四个 blend 值；仍为同族假设，不把 #64 的具体值或验收结果移给 #38。
- #101 只保留为独立对照；不合并 #53/#97/#100/#101 的玩家、版本和验收结论。启动瞬间 installing/native-hooks-unavailable 不当终态。

## 最小修正与构建接线

`CanAutoRestoreRasterNull` 对已记录 blend 仅拒绝 NaN/Inf；有限值按原值回放，禁止夹取到 [0,1]。官方 D3D12 [OMSetBlendFactor](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-omsetblendfactor) 文档定义的是四个 FLOAT 的存储/使用及 NULL→全1；GPU 运算是否夹取不能由准入测试推断。

原 #154 没有更新完整候选生成器的锁定值。本次将 `prepare-multi-api-beta.py::STATE_CURRENT` 从旧 blob `ca61df92bb2d8c689648b12a35e7e1835d294748` 更新到经读取核对的生产 blob **`47598e4068e1ba37327f22a309b17d6b2f0279e2`**。保留所有其它 pins、祖先/dirty/源文件变化检查；不是取消构建门禁，也不以这一个 pin 匹配声称完整 staging 已通过。

Hook、unknown domain、fresh Reset、taint、PSO、viewport/scissor、RTV/DSV 私有描述符、队列、RR 原生调用、资源 lifetime 和格式/曝光逻辑不改。#149 的原因细分与本准入变更分开；将来合并时必须同步有限值诊断合同和最终源 blob，不能复活旧 [0,1] 条件。

## 本次真正执行的验证

- 使用 GitHub 读取的两段生产函数及 `validate-044-compat.py::raster` 提取测试，在 **Linux host** 执行 GCC/Clang、各 `-O0` 与 `-O2`：每组 **172 checks / 0 failures**。这是 host-predicate 验证，不是 Windows addon 或 NVIDIA 运行。
- 扩展覆盖全部 RGBA 通道、最大/最小有限 float、正负 subnormal、负零、观测到的四值同时出现，以及准入不修改原始位。其它准入保护断言保留。
- 反例控制：恢复 main 的 [0,1] 判断产生 **33 个失败**；允许 Inf 产生 **8 个失败**；移除 fresh Reset、taint 门槛分别产生 **1 个失败**。反例退出码均为 1。
- Python 语法检查通过；生成器的修改经反向替换确认仅改变上述一个状态源 pin。重建的输入测试脚本与生成器先核对 Git blob，避免转录漂移。
- Windows WARP fixture 已增加 3 组成对 native draw / production restore 像素对照，不假设越界 blend 的算术结果；原单位区间解析 oracle、NULL 默认值及已有错误 blend 负控制保留。`Gpu::Submit` 原有 fence 完成等待与 allocator/list Reset 不变。

## ROG Windows 实测收口（2026-09-07 15:25 UTC）

本次直接在 ROG Windows 执行，构建源码 **4521edf8aa9ddb6b20077d7f0debc789669c0779**；生产状态 blob 仍为 `47598e4068e1ba37327f22a309b17d6b2f0279e2`。没有重写有限 blend 修复。后续文档提交不改变二进制来源。

- `python scripts/validate-044-compat.py --compiler <fixed-clang++>`：转发38项、生产null-raster172项，全部通过。
- `scripts/validate-command-state-native.ps1 -ToolchainRoot <fixed-toolchain>`：66项、64 KiB工作线程和197函数栈门禁通过；同脚本的默认null路径718项、73次GPU检查通过。
- `scripts/validate-command-state-gpu.ps1 -ToolchainRoot <fixed-toolchain>`：24场景通过；另新增3组成对有限越界blend像素对照通过。`-0,-1,-2,-3` 的原生draw与生产Restore结果相同；不是根据文档猜测clamp行为。
- 同脚本 `-BlendStateNegativeControl`：故意回放错误blend，子进程以1退出且像素oracle明确拒绝；验证脚本返回0，负控有效。
- `scripts/build-multi-api-candidate.ps1`：沿用 [固定依赖与完整命令](VALIDATE_DEV3_WINDOWS.md)，全量staging/源身份检查、生产shader、48场景scaled-work WARP、RR退役54场景及负控、carrier完成状态机/WARP/硬件异常子进程、双语core/nrchain静态分析零诊断和全部DLL编译链接通过。编译有既有unused/novtable等警告；不冒称零警告。

本地证据位于隔离worktree的 `build/pr154-candidate/source/source-manifest.json`、`build/pr154-candidate/validation.json` 和 `build/pr154-build-local.txt`；定点日志为 `build/pr154-{compat,native,warp,warp-negative}-local.txt`。原始日志和二进制不加入源码仓库。

| 实际构建产物 | SHA256 |
|---|---|
| `DLSS5-AI-Rendering-Upscaling-beta0.4.5-dx11-compat-@野生的装机宅-Bilibili.en.addon64` | `e4a25219c979664081d1546a95b355cfccdb5e90384a134a01aabde4c9119144` |
| `DLSS5-AI渲染超分版-beta0.4.5-dx11-compat-@野生的装机宅-Bilibili.addon64` | `a75719b5c87a0fe5e81865891e33f72f6dcf84db5511e4aee7c798caf2bfd7c1` |
| `nrchain_nvngx.dll` | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |
| `dlss5-native-carrier-045-dx11-compat.addon64` | `9e0b9dc3fa05c4fc4b2249ccaf095715f989297a6149074e5cf9deaaff086e06` |

**验证层级**：Windows compile、production WARP已通过；NVIDIA Feature18和游戏runtime未执行。WARP神经平面是确定性测试替身。独立严格snapshot harness要求的D3D12 Debug Layer仍不由这些结果代验。

**CI**：已直接取得run34136127356 / check101787403579的annotation：账户近期付款失败或spending limit限制，任务未启动（steps=[]）。不是源码测试失败，不计为CI通过；未重跑。后续HEAD检查须独立读取。

PR可作为有限blend准入修正进入合并审核，游戏Issues仍open。它不验证 #153 carrier崩溃，也不将 #99历史RR累计计数变成当前成功证据。用于游戏复测时以本次manifest与哈希识别新候选，既有相同显示版本/文件名不能区分源码；已发布包不能替代本次产物。

## 仍需游戏验收

新候选需分别复测 #64/#38 前→后、#99 普通 SR 与 RR；记录同 scope/config/epoch 的成功、录制、提交以及实际 F6 画面。持续旁路时明确提示保留原生链路、用 F8 单 TXT 上传 BUG；已有旧资料不重复索要。#153 carrier 崩溃、#152/#156 loader、#39/#151 黑线独立继续，不借本单验收关闭。
