# 当前主线与交付入口

## 当前主线：0.5.2 Beta 14（维护者2026-10-03接受为开发基线）
| 项目 | 身份 |
|---|---|
| 已交付二进制源码 | `3663affdd77309c3eac9a306849415a1ded4f330`（干净CI staged merge） |
| main基线 | `024f9cc32778c1056db364d76d087ee257da6502`；与二进制源码的Git树均为`5b6f07e34501350b025091ab67d5a313b7739727`，不重命名二进制来源 |
| 合入方式 | #335菜单过渡诊断、#336部分插件分配账目已经合入main |
| 中文 Core SHA256 | `a10947a9902ea3870d48945f3d08bcc0c1c5b142a11deb9c9575f9054144ec35` |
| 英文 Core SHA256 | `a418bddf52a163a413d097f69184584ca4ef802ad0daa412c9557165644696c0` |
| 交付回执 | [052_BETA14_DELIVERY.json](052_BETA14_DELIVERY.json) |
| 用户包 | 双语Core-only OTA + RTX20/30、40/50手动完整升级包；按版本保存；未在线发布 |

Beta14相对Beta13增加F8有界菜单事件和部分资源分配分类账，不改变渲染算法、默认值或公开DLL ABI。
[完整Windows双语构建](https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/36823064779)成功，
实际330文件直接源码清单核验；原canonical回执的历史`generated_source=true`字段保持原样，
实际导出schema为`dlss5-direct-source-v1`、`source_transforms=false`。
本机5090D固定RTX40/50运行库的SR单层、RR单层/三层受控Feature1/13/18另记通过，
不等于游戏、其他显卡或整帧性能通过。Beta13两组运行库的受控结果保留原身份。

**维护者游戏验收（2026-09-29）：** 默认设置下 NR 前置、NR 后置、原生 RR→后置
NR、F6 开关和约 30 分钟稳定性，覆盖两款游戏，结论为没有问题，同意推给玩家。
这只覆盖默认路线；下表的实验项和已知问题尚未完整验证，不因合入 main 而算通过。
**Beta14维护者记录：** 异环原生RR下的Esc过渡表现于2026-10-02接受；单层→双层→单层
已知分配468.38→681.63→468.38 MiB，支持此次已知资源回收，不证明完整显存无泄漏。
随后61%私有SR已实际执行，但静态碎边/颗粒与维护者报告的闪烁使当前画质未通过。
据各版交付记录，UI1 起的两项新功能默认关闭（低分辨率细节增强、去除暗噪），
私有 SR 和两个空间实验同样默认关闭。

## 发布给玩家

- Manager 默认版本在 `smartLanny/DLSS5-Manager-ZJZ` 调整，本仓库不改 Manager pin。
- Beta14 Core-only OTA不带NR；两组NR库独立保留，另提供含NR/人脸资源的手动完整包，当前Manager不能导入。**确认再分发
  许可之前，公开发布只放 Core、nrchain、人脸资源和许可说明**，玩家继续使用本机运行库。
- 发布前按 `docs/REPOSITORY_AUDIT.md` 审查载荷，保留上一版可回退。

## 开放问题

| 问题 | 现状 | 入口 |
|---|---|---|
| Esc / UI 场景丢 NR | A：异环本轮Esc表现已有维护者通过，根因未证实；B/C：独立UI+3D预览场景继续按实际证据区分，不扩大为全部通过；Beta14已有过渡诊断 | #317 |
| 显存占用偏大 | Beta14已有部分插件自有分配分类账，包含未知所有者；此次切回单层下界回收，但完整模型内存、驻留量和相对D13优化未证明 | #298 |
| 长时间内存增长 | 已有驻留回收、退休和严重提交压力旁路，根因未证实 | #298 / #293 |
| 私有残差 SR | 游戏中已证明部分配置实际执行和局部GPU完成；61%普通恢复77FPS、私有69FPS为维护者观察，碎边/闪烁质量未通过，默认关闭 | #309 |
| 两个空间降噪实验 | 默认关闭、可手动比较；细线质量门未通过 | #309 |
| 时域来源与历史 | 游戏中未证实历史采用；局部 GPU 时间不是整帧成本 | #274 / #296 |
| 整帧新增成本 ≤3% | 未证明 | #277 |
| RTX20/30 实卡 | 只有 5090D 上的接口验证 | — |
| 2077 | unified10、0.5-d13、0.4.7 均在首次 NR 创建阶段故障，无混版证据，根因未定 | — |

普通恢复仍是建议基线。原生RR→后置NR已交付；低分NR→一次原生RR或绕行、第二套私有RR未实现。

## 分工（维护者 2026-09-29 决定，见 #311）

- **Claude 维护会话：** main 整合（审查后合并 PR）、发布 tag、PR 与 issue 清理、
  文档维护和源码 pin 冲突处理。
- **执行 agent：** Core 功能开发、Windows 完整构建、打包和硬件验证；在任务分支上
  开 PR，不直接合并 main、不关闭他人 PR、不打发布 tag。
- **维护者：** 游戏验收、发布与 Manager 默认版本的最终决定。

## 下一步（按顺序）
1. **#309：** 按[最新收敛规划](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/309#issuecomment-5959036328)，开发侧一份16帧内部证据、定位后同任务最小修正；比较匹配空间参考和仅增加私有SR修正的候选，再集中交一个修后包。
2. **#309 NR→一次原生RR：** 可独立并行做零编辑透传/小修改及真实导引安全检查；首轮N=R，不阻挡SR交付。第二套私有Feature13暂缓。
3. **#298/#317：** 继续分析已有显存和独立UI预览证据，不重做已合诊断或要求维护者重复已接受测试。跨API transport另线，不混入本批碎边修正。

**已完成 #321（#327，2026-09-30 合入 `0e1ba85c`）：** Core 直接编译 `src/`，16 个 `patch-*.py`
已归档，结构见 [SOURCE_LAYOUT.md](../maintenance/SOURCE_LAYOUT.md)。维护审查在自托管 runner 上用原
生成器独立重建 `cd8ba700`：manifest SHA256 等于 Beta 13 回执的 `6d15f8ee…`，PR 头 301 个 Core 文件
300 个逐字节相同，只有 `scripts/build.ps1`（源码身份改为读取导出回执）不同。新编译的 Core DLL 哈希
与已发布 Beta 13 不同，不声称二进制等价，也不是 NGX 或游戏验收。

**版本号：** main上的`scripts/core-source-layout.json`为`0.5.2-beta.14`。下一行为改动先更新为
`0.5.2-beta.15`，不能用Beta14名字发布新行为二进制；本轮OTA只复用已核验Beta14原二进制。

**本机维护项已完成（2026-09-30）：** `v0.5.2-beta.13` tag 已推送，已并入的旧分支已删除，
`codex/317-…`、`codex/293-298-…` 先打 archive tag 后删除。

## 从哪里开始
- [新电脑接续与依赖](../../START_HERE.md)
- [Beta14交付与范围](052_BETA14_DELIVERY.json)、[最新#309规划](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/309#issuecomment-5959036328)
- [Beta 13 历史已知问题](../handoff/BETA13_KNOWN_ISSUES.zh-CN.md)（优先采用上方最新状态）
- [Beta 12 手测单](052_BETA12_TEST_CHECKLIST.zh-CN.md)（Beta 13 行为相同）
- [归档索引](../maintenance/ARCHIVE_INDEX_20260926.json)：177 个旧远端分支、本地工作树和别名的保存位置
- 已被 #313 包含而关闭的 PR：#308、#316（整条分支在历史中）、#318、#319（修复已移植，`src/` 新增行逐行核对）

## 验证口径

任何系统上都可以先跑 `python3 scripts/run-portable-tests.py`（说明见 `CONTRIBUTING.md`）；
它不替代 Windows canonical 构建。
源码审查、编译、CPU、WARP、硬件 shader、NGX 推理、托管 CI 和游戏验收分别记录；
跳过、失败、取消、未启动都不是通过。托管 CI 仍需 `ENABLE_HOSTED_CI=true`，目前为 `false`。
维护者电脑上的自托管 runner 受 `ENABLE_SELF_HOSTED_CI` 控制：每个 PR 跑快速测试，
完整构建需手动触发或给 PR 打 `full-build` 标签，说明见 `docs/maintenance/SELF_HOSTED_RUNNER.md`。
首次记录（2026-09-30）：main `7ea8ae15` 上快速测试 69 通过 / 0 失败 / 2 跳过；完整构建
（run 36712552190）exit 0，约 55 分钟，`source-manifest.json` SHA256 `983ae6a4…dc29`，
`validation.json` SHA256 `4b7e6174…0ee1`。这是本机 CPU/WARP 构建证据，不是 NGX 推理或游戏验收，
也不重新命名任何已发布二进制。该次运行的 `*-checks` 门禁目录被旧清理逻辑误删（#326 修复），
主输出目录保留。
持续旁路必须请求 F8/日志反馈，旁路带来的高帧率不是优化。

## 历史

- Beta 1–14 各版验收、交付与回退：`docs/interop/052_BETA*_*.md/json`，保留原身份。
- 合入前的完整状态页：[CURRENT_STATUS.pre-integration.md](../history/beta13-integration-20260929/CURRENT_STATUS.pre-integration.md)
- 主线归位前的规则、README 和状态：[pre-mainline-20260926](../history/pre-mainline-20260926/)
- 0.3/0.4 与早期 0.5 文档、0.4.7beta corefix / dev / dline / D21 逐版记录：见 [docs/history](../history/README.md)
- 旧发布线脚本：[scripts/archive](../../scripts/archive/README.md)
