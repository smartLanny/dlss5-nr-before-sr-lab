# 2026-09-07 P0/P1/P2 定向交接：RR 拒绝阶段诊断

本批基线为 `main@7d17c10d8abf0b6342b0bcf4ff0e4a1e164647d3`。
历史 multiapi-dev3 已整合到 main；本批不重建旧集成线，不合并 main，不打包或发布。
这是 **P0 诊断缺口的最小补丁**，不是游戏兼容性根因已修复的声明。

## 已读取的证据与缺口

| 优先级 | 实际来源 | 本批结论 |
| --- | --- | --- |
| P0 #99 / #15 | [#99 dev3 回帖](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/99#issuecomment-5569032596)，及 main 的 RR/命令状态源码 | dev3 后置采集窗口 `scope.nrSuccess=0`，RR attempts=4191、recordings=0，尾态 null-guard；历史 NR=5030 不能代验当前 RR。用户另报前置从未生效，不能用这份 mode=1 摘录证明所有 mode=2 场景。 |
| P1 #64 | [fix1 capture-info](../../bug-inbox/issue-64/20260907-NeroEggs_0.4.5-fix1_elden-erss/capture-info.txt)，完整读取 | source=e5ce1210；前置历史 NR=2582，后置同 config=11 / epoch=9 的 start/end NR、transfer 都为 0。 |
| P1 #38 | [fix1 capture-info](../../bug-inbox/issue-38/20260907-NeroEggs_0.4.5-fix1_sinking/capture-info.txt)，完整读取 | source=e5ce1210；前置历史 NR=5049，后置同 config=24 / epoch=6 的 start/end NR、transfer 都为 0。没有 ERSS，不把共同阻断归咎 ERSS。前置观感弱另留问题，不用计数声称画质正常。 |
| P2 #39 | [dev3 黑线回帖](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/39#issuecomment-5569032830) 与 [后续 DM 澄清](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/39#issuecomment-5569435568) | dev3 的 NR scope 有数千次成功，黑线仍在。最新文字反馈称变淡但未消失；新 DM 包的 build 尚未独立核验。fix1 HDR/颜色正例不用于结案。 |

#99 回帖所列 `20260907-shuhai_0.4.5-multiapi-dev3_htgame-rr` 目录在固定
main 快照中返回 404；该 Issue 目录读取结果只有较早云端归档。
因此本批没有假称读到 dev3 原 TXT 或新私聊 ZIP，P0 当前依据是回帖摘录。
#39 dev3/新 DM 原包也未读取。已在本机入库的材料优先由维护端读取，
不重复向玩家索要既有附件。

本会话实际调用 ROG Windows `open_workspace` 被拒绝：
`FORBIDDEN: This conversation does not support developer MCPs`。
发现插件为 ENABLED 不等于工作区成功打开。纠正误连后未继续使用 Mac。
本批只有 GitHub 源码读写与云端可移植逻辑测试，没有 ROG 运行结果。

## 根因假设及如何区分

**P0，优先排查共同命令状态准入。** `null-guard-graphics-state-used` 是
有意义的持续尾态，但当前 `lastStage=4` 把 RR 前检查失败、准备拒绝、
RR 后检查失败、后续录制失败和生命周期校验拒绝混为同类，无法只凭
attempts/recordings 定位边界。也不能把早期 `installing=1` 或首次
`native-hooks-unavailable` 当作永久故障；先看启用后的尾态和同窗口增量。
`rrLiveHandles=0` 是另一时刻的观察，不推翻此前 RR 调用计数。

可证伪的排查顺序如下：

1. 如果 RR 前状态检查挡住执行，新窗口 `beforeStateRejected` 增长，
   而不是把它归为后置回填失败。
2. 如果准备成功但原生 RR 改变后的状态无法恢复，`afterStateRejected`
   增长；仍须读对应原始日志的 reset/guard/dynamic/blend，不能套用旧前状态。
3. 如果状态检查可过但资源准备或录制失败，分别看 `prepareDeclined`、
   `finishDeclined`；如果成功准备、原生 RR 成功后 generation/handle/
   ownership 校验不通过，则 `finishGateRejected` 增长。原生失败看既有
   `nativeFailures`，不重试已执行的 RR。

**P1，不能再假设“只放宽前置、忘了后置”。** 两份实际 fix1 metadata
均有 hooksReady/guardCovered/freshReset=1、tainted=0、dirty=0、
dynamic=0x8、vertex/stream mask=0。当前 main 前后共用
`NativeCommandStateScope`；`CanAutoRestoreRasterNull` 已允许合法 blend
状态，`CommandStateBlendFactorValues` 也已处理空指针默认值。
旧 schema=1 没有保存实际 blend 四值，`0x8` 只说明调用过相关 setter。

下一步先从现有 dev3/兼容增强 F8 读取实际四值和对应 token/Reset。
若四值不在当前有限恢复契约内，需查明来源与合法恢复方式，不能删保护。
若所有准入条件都真实满足仍被拒绝，应核对同次状态、文件身份与采样关联。
本批不替换游戏 blend 值，不钳制成默认值，不用前检查快照跨过原生 SR/RR。
#64/#38 未以本 PR 登记为运行时修复。

**P2，保留独立画面缺陷。** 从“光照保护相关黑线 + NR 连续成功”的
组合，优先怀疑光照保护的局部合成/边缘行为而非完全没跑 NR；这只是
现象导向假设，尚未完成 shader 根因定位。应在同 build、同场景比较
光照保护 0 与已有 0.2/0.6/1.0 材料；HDR/颜色正例不能替代该对照。
本批不改 shader、HDR 策略或强度系数。

## 最小补丁

三个生产文件分别增加计数存储、实际边界计数、同一个 F8 TXT 的字段。
新增字段位于 `start.post.rr.*` / `end.post.rr.*`：

| 字段 | 精确含义 |
| --- | --- |
| beforeStateRejected | 实际 RR 前 `NativeCommandStateScope` 未激活。 |
| prepareDeclined | 前检查已激活，但 `PrepareAfterSr` 未返回 active。 |
| afterStateRejected | CanFinish 已允许继续，但实际 RR 后 state scope 未激活。 |
| finishDeclined | 后检查已激活，`FinishAfterSr` 返回 false。 |
| finishGateRejected | 已成功准备且原生返回成功，但 CanFinish 拒绝继续。 |

这些是进程生命周期累计的独立原子观察，不是互斥帧数，不是事务一致快照。
例如同一次前检查拒绝后，原生 RR 仍可能失败，两类计数都增长。
只比较同进程 start/end 增量；`commandState` 仍是独立最新样本，不能
强行和某个 RR 累计计数关联为同一帧。旧 lastStage 与已有字段保持不变。

没有新增 hooks、GPU 命令、资源分配或游戏分辨率改写。
所有分支仍调用原生 RR 恰好一次并原样返回状态；保留前后独立快照、
before.Restore 的位置、after.Restore、ownership/chain 锁和异常生命周期隔离。
意外安全旁路仍保留原生链路，并通过现有提示要求 F8 TXT / BUG 反馈。

## 本批实际验证

云端 Linux、clang++、API 替身；**不是产品 Windows 编译通过**。

```text
python -m unittest discover -s tests/multi_api -p test_rr_post_diagnostics.py -v
```

测试直接包含生产 `nr_post_settings.inl`、`nr_rr_post_runtime.inl`、
`nr_post_feedback.inl` 及真实 CanFinish 策略；D3D12/NGX/Win32 边界为替身。
修改前同一测试失败于缺少 `end.post.rr.beforeStateRejected`。
修改后一个 unittest 中的 50 个 case 通过，涵盖两种请求模式、全部五类边界、
原生失败、前拒绝同时原生失败、禁用/零强度/identity、锁竞争、
handle/generation/feature/ownership 变化，以及 F8 start/end 增量。
另用 O2 构建同一替身 harness，50 个 case 通过。

四个独立负控均被测试拒绝：错记前/后计数、遗漏 before.Restore、
遗漏生命周期 quarantine、重复原生 RR 调用。负控未写入交付源码。

验证边界：此测试不能证明真实 hook 状态采集、blend 值、资源有效性、
GPU 同步、NVIDIA Feature18、图像正确性或性能。未编包、未启动游戏。
ROG Windows 产品构建、WARP、Shader 和游戏复测均待完成。
托管 CI 单独以 PR 实际检查为准，不继承 main 旧的本地/CI 结论。

## ROG 接续验收

先核验实际源码和包身份，再使用 #99 的现有原 TXT 定位最终状态，区分
RR 关闭下的普通前置与 RR 开启下的有效后置。新诊断候选只在产品构建及
已有相关 Windows 门禁通过后进入游戏；同场景 F6 与一次 F8，检查新增
边界计数的窗口增量、scope NR/transfer 和对应状态。始终保留安全旁路。

#64/#38 共用后置回归方案，但各自记录游戏结果；不强拆原 ERSS、不改 FG。
#39 保留独立 HDR 黑线验收，不因 NR 成功或颜色正例通过而关闭。
#121/#148、#139/#140 未展开；#127 与 #141–#147 本批未处理。
#99/#15/#64/#38/#39 全部保持开放，不使用自动关闭关键字。
