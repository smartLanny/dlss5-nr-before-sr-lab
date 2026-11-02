# #309 B1/B2 正式技术审查汇总 — 2026-10-08

**runtime safety 与 functional contracts 两轴均 PASS，可执行缺陷为 0。**
结论仅覆盖冻结的 B1/B2 隔离 Color identity 接入及其受控合同，不表示
Claude 已合 main，也不表示正文 §5 完整算法、产品接线、发布或游戏验收通过。
main 整合继续遵守 #311 的所有权边界。

## 审查与运行身份

| 身份 | 精确来源 |
|---|---|
| 审查 base | `024f9cc32778c1056db364d76d087ee257da6502` |
| 审查 HEAD / PR #342 | `6a4d3a32bbb4cfa7282db60f7b5a43cc6b811858`；开始、结束均 clean，12 个新增文件，生产 `src/` 无差异 |
| B2 完整 Core adapter / native host 实际编译源 | `9b2636eb6cd80b540f80194fc6be771d14709c0b`，不是审查 HEAD、base 或当前组合树 |
| B1 静态合同审计源 | `f9ef953faad0a31d280d27adbbe411433a352a47` |
| 组合 r2 CPU 修复快照 | `0660260f52e84eb729f70a83325646f5e11df9a0`，不继承上述 B2 运行身份或完整 canonical PASS |

两轴均为正式只读审查，复核真实源码、封存回执及哈希，没有重新执行
编译、GPU、NGX 或游戏。独立重算的完整 Core 333 文件、native 50 项
immutable 输入/对象/运行库，以及 48 份已保存输出全部匹配。

## PASS 的证据范围

- [B1 合同](RR_B1_STATIC_CONTRACT_20261008.json)：53 个角色、137 个官方键。
  真实游戏各角色仍 `runtime_verified=false`，actual 未采集，保持 unknown。
- 隔离 WARP 模块：正向 304 项；真实设备移除冷/已录制 17/20 项；40 个
  官方非 Color 资源 alias 拒绝；六项精确移除负控均编译成功后 exit 41。
- [完整 Core adapter](RR_B2_NATIVE_CONTROL_20261008.md)：七模式合计 282 项。
  首条 copy 前的真实 recording 租约、录制释放与 exact fence 双门、普通路径
  一次原生调用及 fatal 隔离、typed/void Color 恢复、quarantine/off 和真实 scope
  均通过；移除 recording 发布及 Restore 各在指定断言 exit 41。
- 原生 NVIDIA A/A2/B 三个独立进程，各创建一次 Feature13、Evaluate 32 次
  （16 warm + 16 observe）；仅 B 获取并退休 32 个实际 Core copy leases。
  R=N=1280×720、O=1920×1080，静态不透明平面、零 jitter/MV、FG off。
  非单位 exposure 原样透传。A–A2 与 A–B 的全部观察帧 RGBA FP16 位相同，
  输出有限非黑且有空间/时间变化。没有 NR、额外 private RR 或产品入口。

ClearState 采用**同源生产组件门与 B2 adapter 的组合证据**：实际 `9b2636eb`
的生产 command-state 宿主覆盖失败 Reset→ClearState，拒绝认证 guard 前
recording、拒绝清除 protected/render-pass taint，并验证旧 token/新 native
lifetime 及双 scope 恢复；
默认门 67 项、0 失败、64 KiB worker PASS，native-record exit 0，省略恢复
负控 exit 42。B2 使用同一真实 Capture/Restore，并另有移除 Restore 的
精确 exit 41。没有声称七个 B2 模式直接调用 ClearState。

## 仍未知与下一门

真实游戏的每角色内容同源、format/view/subresource/subrect、曝光语义、
producer queue/fence、资源/模块/feature 生命周期及显示采用仍 unknown。
本次零编辑控制序列不证明运动中的三份 phase、非零 jitter/MV、反射/透明、
编辑传输、画质收益、VRAM 或 FPS；未预写 sentinel，也不声称独立写掩码
或一般 NGX 逐位确定性。

按 [#309 执行单](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/309#issuecomment-6034217346)，
下一唯一工作是隔离验证 **PrivateSceneRR Feature13 的 R→R 能力**，先证明
factory 域及实际创建/推理能力，不能借用 Feature1 的证明。后续仍须分别过
三份相位、HDR/exposure、资源/历史隔离和成本门，再评估
`PrivateSceneRR(R→R) → NR(clean R) → rephase edit → native RR(R→O)`。
两次 RR 的最终方案及低分 NR→一次原生 RR 均未据本次 PASS 升级为可用。

组合 canonical r2 在 `373cec0e` 的旧 CPU fixture 栈溢出失败，保留原回执；
`3e9659ab` 仅将测试 FeedbackSession 迁至 heap，33 项正向及原序列化反例
仍有效。截至本次汇总，最终组合 canonical 未通过，r3 未开始。

私有审查原件以 `local-evidence:rr-b1-20261008` 下的
`B2_RUNTIME_SAFETY_REVIEW_6a4d3a32.md` / `B2_FUNCTIONAL_REVIEW.md` 封存；
配套回执使用 `local-evidence:rr-b1-20261008`、`local-evidence:rr-b2-20261008`
及 `local-evidence:combined-20261008`。不提交私有磁盘路径、原始像素或厂商二进制。
