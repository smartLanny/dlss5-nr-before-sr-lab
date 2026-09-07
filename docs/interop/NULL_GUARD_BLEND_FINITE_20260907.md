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

**ROG 边界**：本次在 Windows 建立隔离 worktree、读取反馈、编辑 WARP fixture 成功；运行验证命令时工具返回 `FORBIDDEN: This conversation does not support developer MCPs`，重新发现后执行及读取仍被拒。不是 C++ 编译报错，也不能据此认定本机服务掉线。未取得本次 Windows 编译、WARP、NVIDIA、游戏结果；未生成新 OTA。原 product、其他 worktrees 和未提交文件保留。

## Windows 接续与验收（尚未执行）

先 fetch 并检查 PR #154 最新真实 HEAD，使用隔离工作树；确认 `HEAD:src/nr_command_state.inl` 与上述 pin 一致。ROG 上此前只写了 fixture、尚未切到修复提交的旧隔离目录，不能直接拿它构建候选。

```powershell
python scripts/validate-044-compat.py --compiler "$env:DLSS5_LLVM_MINGW_ROOT/bin/x86_64-w64-mingw32-clang++.exe"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/validate-command-state-native.ps1 -ToolchainRoot $env:DLSS5_LLVM_MINGW_ROOT
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:DLSS5_LLVM_MINGW_ROOT
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/validate-command-state-gpu.ps1 -ToolchainRoot $env:DLSS5_LLVM_MINGW_ROOT -BlendStateNegativeControl
```

随后用固定上游完整 staging / addon 构建并核对 source-manifest，候选身份应与已发布包区分。**旧 A 包、旧 dev3 或 dx11-compat 原包只能提供基线，不能验证本次新增修复。**

新候选需分别复测 #64/#38 前→后、#99 普通 SR 与 RR；记录同 scope/config/epoch 的成功、录制、提交以及实际 F6 画面。持续旁路时明确提示保留原生链路、用 F8 单 TXT 上传 BUG；已有旧资料不重复索要。#153 carrier 崩溃、#152/#156 loader、#39/#151 黑线独立继续，不借本单验收关闭。
