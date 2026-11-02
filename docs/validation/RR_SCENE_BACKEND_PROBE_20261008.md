# #309 SceneRR 同网格后端与组合检查 — 2026-10-08

**实际 PrivateSceneRR Feature13 的 1280×720→同网格能力已建立。**
原生硬件宿主成功；原 Python runner 的计数失败保留，用原始输出完成两处
统计窄修及独立复核。没有产品新路线、模型相位、画质收益或游戏验收结论。

## 准确身份与能力范围

| 对象 | 身份 / 结果 |
|---|---|
| 当前 main | `024f9cc32778c1056db364d76d087ee257da6502`，实时核对未变化 |
| Scene probe 源 | `3882f77517e754e6c2ecf168f0efebe32f2ab95f`，只新增隔离 host/runner |
| 实际完整 Core | `0660260f52e84eb729f70a83325646f5e11df9a0`；337 文件 manifest，dirty=false |
| Core manifest SHA256 | `a197888019661b82e0e81b2a4a92e7bb54c274f42404ded9ff1131fa51ae9caf` |
| 实际 host EXE SHA256 | `0dddcdace1ad360e8e8ad7a610828ef3bcad5f68d8c78eeb4f5010a8a59c9222` |
| 原 hardware log SHA256 | `ed4d673e2af94bfa39032e6d08bd3002740180c5473435cc4b3edf22724c3706` |
| 原失败 runner receipt SHA256 | `7eb85b0391fa1a7d8e1ba06c21a03673460bd986170360cd0a69562e55a0cdff`，未覆盖 |

固定 R=1280×720、quality=DLAA5、flags=75、RR preset=Default0，单 sample/mip、
无 subrect。先从同一真实 NGX 初始化域查询 RR capability 和 optimal callback，
确认实际 R→R，才创建独立 child；没有扫尺寸、替换 Feature1 或第二次 Init。

实际宿主 exit0，5660 项检查：一次 Bootstrap、一次原生 parent Create、一次
private child Create、32 次 child Evaluate（16 warm + 16 observe），parent Evaluate、
NR 和 FG 均为0。34 条真实生产录制租约与34条退休一一配对；每个 opaque
调用之前固定 raw/clean/七份 guides 和 CPU 矩阵 owner，之后恢复真实命令状态。
真实队列完成和真实 Reset 双门后才能释放；child 与三个独立参数销毁均成功。

观察期16份完整 RGBA16F 输出逐项哈希匹配，全部 finite，RGB 非零、有空间
变化且相邻帧有变化。每次 Eval 前输出填 sentinel；每个 RGB 都被真实调用覆盖。
这只建立本受控场景的执行与输出事实，不能外推普遍确定性或降噪质量。

六个独立负控均命中指定运行 oracle：缺 capability、缺 callback、错误 optimal
结果、错误 parent generation 在 private Create/Eval 前 exit42；移除 recording
publication 在 child Create 前 exit41；移除 opaque 后 Restore 在 Create1/Eval0
时 exit41。均复用实际 parent/observer/生产租约，不改生产计数来伪造通过。
cold off 在任何 GPU、Init、分配之前返回。pending-off 尚未由本 slice 验证。

## 原失败、诊断修复与独立复核

原 runner 的非锚定 `LEASE .*` 同时匹配 `SCENE_RELEASE`，误计35而非34，
故 Python 整体结果仍保存为 failed，不能把它重命名为成功。最小修正锚定
行首；CPU 重读同一日志得到精确34，没有重跑模型。

另修时间响应只记录不影响通过的问题：空间合法但16帧完全相同的输出集，
现在报告“本受控序列未建立时间响应”，不判模型失败或画质缺陷。因为此例
底层 radiance 固定而噪声变化，理想去噪保持稳定也是合法解释。
真实16帧正向统计完全不变；重复同一真实输出16次的 CPU 反例由旧代码接受、
新代码精确拒绝。两处修复与54项原 immutable 输入均核对，原失败回执未变。

GPT-6 Astra high 对新3882运行时切片正式只读审查：**runtime safety PASS，
P0/P1=0**；上述两项诊断 P2 修复后定向复核 PASS，无待修项。没有借用旧 B2
审查冒充新 factory/child 通过，也没有重跑 NGX 凑结果。

## 同一准确组合 Core 的补充检查

- 4K 私有 SR 预算放开执行：fixture-only R=2228×1253、N=O=3840×2160，
  实际 Feature18 单层后接 Feature1，537项通过；private Create1/Evaluate3、
  合成采用3、提交5，退休后自有平面0。峰值自有平面1,103,036,416 B，
  确实超过旧768 MiB限制。测试未改 Core、冻结#340、运行库、配置或游戏。
  首次私有 wrapper 缺 helper 的准备失败另保留。DXGI进程变化包含厂商/缓存，
  不是模型准确字节；短窗口及诊断采集不能作为 FPS、稳定占用或质量收益。
- 完整真实 Core F8 CPU 七种时钟正向通过：known-clock 1973项，其他各1578项；
  35项容量/结束退休/隐私精确负控及另两项 TXT/ZIP 负控有效，共37项。
  最大 mandatory tail 309,214 B ≤ 361 KiB，metadata 594,910 B ≤ 640 KiB，
  全文本782,598 B ≤ 832 KiB；32 context、256 slot、32 modelEvent 的 start/end
  明细均完整，未知时钟保留 null。它不构成显存测量、GPU生命周期或 canonical。

## 下一门与仍未知

Astra 已准入真实 D3D12 raster/shader 数学 oracle，然后才是预注册独立 history
相位/HDR观察。直接 NGX jitter 符号、实际 clean 相位与曝光尺度、clean 对应
depth/MV、跨表面/显露仍 unknown，不能根据最低残差或最清晰画面自动选符号。
完整 `SceneRR(R→R) → NR(clean R) → rephase edit → native RR(R→O)` 仍未接产品，
这是两次 RR；低分 NR→一次原生 RR 的独立可行性没有借此升级。

新开发侧3D测试台按维护者要求独立评估官方 NRD-Sample 与 RTXPT，先确定
适配价值/成本；NRD 不等于 Feature18，必须验证准确当前 Core/模型的真实
调用和采用。画质采集、性能测量分开，GPU工作串行，不因此暂停原 issues。

最终组合 canonical 尚未通过，r3尚未开始；#338/#340、Beta14回退、现用
Beta15/配置/运行库保持不变。没有部署中间包，维护者最后统一游戏测试。
0.4.7帧数差距仍没有因果闭环，停止的私有SR无净收益候选不重新送测。

私有证据键：`local-evidence:scene-rr-20261008/backend-probe-compile-r2`、
`backend-probe-reanalysis-r2`、`runner-diagnostic-fix-r1`、`private-sr-4k-r2`；
`local-evidence:combined-20261008/full-f8-066`；
`local-evidence:rr-b1-20261008/SCENE_RR_RUNTIME_REVIEW_3882.md`。
原始像素、磁盘路径、厂商 DLL 及原始反馈不上仓库。
