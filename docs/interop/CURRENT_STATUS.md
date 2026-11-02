# 当前源码、交付基线与验证状态

## 当前源码：0.5.2 Beta17 开发候选

`main` 是唯一持续开发分支。版本身份以 `scripts/core-source-layout.json` 为准；
新源码合并不等于安装包发布，也不将 Beta13/14 的游戏结果转移给新候选。

| 本次集成内容 | 来源与证据 |
|---|---|
| Beta14 交付与 Core-only 打包整理 | #337；原二进制和回执保持身份 |
| 原生命令列表死亡与 NR recording 退休 | #341；精确 lifetime/fence 合同保留，原分支有 Windows canonical 回执 |
| 全局与五层独立网络请求 | #346；实际 Create/caller/consumer CPU 负控、运行库身份与重放验证 |
| Continuous SDR 可选色控 | #345；明确 SDR 合同准入，Legacy 与 Generic 关闭默认保留 |
| 恢复推荐画质同时恢复 Legacy | #347；普通加载和单层模型操作不重置算法 |
| 画面/模型分组、当前配置摘要与继承按钮 | [界面验证](../validation/UI_OPEN_SOURCE_REFRESH_20261103.md) |
| 命令状态快照减少无效常量复制 | [生产函数回归与微基准](../validation/COMMAND_SNAPSHOT_COPY_20261103.md) |
| 已有 GCC 编译告警修复 | [便携检查](../validation/PORTABLE_PR_REVIEW_20261103.md)，未降低警告或断言 |
| 有界显存明细、同身份趋势与组合 F8 容量 | #343 适配；[实际 writer/collector 与容量负控](../validation/ISSUE330_COMBINED_CLOUD_20261103.md)，不是显存节省证明 |
| Streamline PCL 安装移出工厂回调 | [锁顺序、缓存指针与 owner 边界](../validation/STREAMLINE_FACTORY_LOCK_20261103.md)，仍需真实 SDK/游戏验收 |
| F8 单 TXT 直接整理问题报告 | [解析与隐私回归](../validation/F8_TEXT_REPORT_20261103.md)，保留部分反馈和原始文件 |

网络值是请求，不能凭版本字符串、创建成功或菜单选择认定模型已经执行。
顶部实际运行状态与当前配置摘要分开；F8 按既有格式保留请求、原因和证据。

## 已交付基线：Beta14

| 身份 | 值 |
|---|---|
| 原编译源码 | `3663affdd77309c3eac9a306849415a1ded4f330` |
| 当时 main | `024f9cc32778c1056db364d76d087ee257da6502`，与原编译源码同 Git 树 |
| 原交付回执 | [052_BETA14_DELIVERY.json](052_BETA14_DELIVERY.json) |
| 完整构建记录 | [Windows run 36823064779](https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/36823064779) |

Beta13 默认 NR 前置、后置、原生 RR→后置 NR 和 F6 已有维护者的两款游戏记录；
Beta14 的异环 Esc 表现及单→双→单层已知分配回收另有记录。这些范围保留原身份，
不是新版、所有显卡或所有实验的验收。私有 SR 的碎边、颗粒与闪烁尚未完整验证。
UI1 起的两项新功能默认关闭（低分辨率细节增强、去除暗噪），私有 SR、空间实验及
Continuous SDR 同样保留各自默认与明确的手动启用行为。

## 继续保留的研究 PR

| PR | 未合入原因与下一步 |
|---|---|
| #340 私有 SR / 可用显存准入 | 包含 #338 的实现，需完整组合 canonical、实际 GPU 分配、NGX 与画质/成本对照 |
| #342 双 RR / 原生 3D 测试台 | 147 文件为研究、脚本、测试和回执，没有修改产品 `src/`；仍需完整宿主可复现与接入验收 |

#343 的源码已适配本候选：337 KiB tail 与独立退休 24 KiB 合计 361 KiB，
TXT 容量声明、ZIP 清单和解析边界同步修复。Linux 的实际新增 writer、收集与解析
链已通过；完整 Core TU 和 Windows/WARP 由本候选 canonical 单独执行，结果以
对应源码 SHA 和 job 为准。原 PR 的关闭/归档不自动关闭 #330 或玩家显存问题。

保留分支和研究证据，不把尚未采纳的实验标成产品功能，也不因关闭/合并 PR 自动关闭
对应的玩家问题。

## 尚待验收的问题

| 范围 | 入口 |
|---|---|
| 菜单、独立 UI+3D 预览的 NR 中断 | #317 / #339；原生 destruction notice 不足时保留明确旁路 |
| 冷启动停顿、模型/槽驻留与长期增长 | #293 / #298 / #329 / #330 / #331 |
| D13/D21 整帧开销、RR+FG 的 GPU 利用率 | #272 / #273 / #277；本次 CPU 微基准不能回答 FPS |
| 游戏颜色、近黑、人脸、边缘、运动与 HDR | #274 / #309；Continuous SDR 的保护条件不能放宽到未知输入 |
| 多 API provider、FG owner 与跨模块寿命 | #164 / #167；双 presenter、辅助 swapchain 和设备探测须独立验证 |

普通恢复与一层仍是起点。RTX20/30 实卡、模型网络混用、WARP/NGX、实际界面渲染和
各游戏帧时间分别验收；缺失、跳过、取消、失败都不能记为通过。云端候选只有全部
构建门禁成功后才产生 Core-only 工件；不是正式 Release，不改变 Manager pin、
游戏目录或运行库。[集中验收顺序](../handoff/BETA17_CANDIDATE_CHECK.zh-CN.md)供取得 Windows 后使用。

## 开发与发布

- [开发入口](../../START_HERE.md) · [源码结构](../maintenance/SOURCE_LAYOUT.md) · [贡献指南](../../CONTRIBUTING.md)。
- 便携入口：`python3 scripts/run-portable-tests.py --verbose`；完整构建仍走 `scripts/build-unified-candidate.ps1`。
- 公开 PR 的源码/CPU 检查使用临时 Linux runner；原有 Windows 托管门禁和本机 runner 开关继续独立控制。
- [公开标准 Windows 构建](../maintenance/CLOUD_WINDOWS_CANDIDATE.md)由明确标签或手动输入启动；完整 face、FFX、SM5、WARP 和 F8 组合门禁均保留，不需要维护者本机在线。
- [本机 CI](../maintenance/SELF_HOSTED_RUNNER.md)只自动接收符合身份条件的维护者 PR；外部代码先审查，`full-build` 标签不覆盖来源限制。
- 持续旁路必须请求 F8/日志反馈；反馈公开前脱敏。旁路引起的帧数变化不是性能优化。
- 原创源码为 [MIT](../../LICENSE)，第三方声明保留；公开包不附 NVIDIA NR 模型/运行库。
- 二进制发布、Manager 默认版本和游戏验收仍各自记录。此前协作分工见 #311，本轮维护按用户明确授权整理与合并。

## 历史

[Beta1–14 交付目录](.)、[历史文档](../history/README.md)、
[分支与归档索引](../maintenance/ARCHIVE_INDEX_20260926.json)保持原版本与来源。
