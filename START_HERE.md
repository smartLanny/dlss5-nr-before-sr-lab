# 新电脑与下一位开发 Agent 从这里开始

当前开发基线为 **0.5.2 Beta 14**，维护者于2026-10-03批准继续使用。已交付二进制编译源码为 **`3663affdd77309c3eac9a306849415a1ded4f330`**，与main基线`024f9cc32778c1056db364d76d087ee257da6502`的Git树相同；后续文档提交不改变二进制身份。[Beta14身份、验证与OTA](docs/interop/052_BETA14_DELIVERY.json) / [中文更新](docs/interop/052_BETA14_CHANGELOG.zh-CN.md) / [后续研究任务](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/309#issuecomment-5959036328)。

- 原开发分支 `feat/309-post-private-sr`（PR #313）已合入 main。新任务从最新 main 开短期分支。
- [完整新机说明](docs/handoff/BETA13_NEW_PC.zh-CN.md)：依赖、恢复、构建、验证和证据入口。
- [已知问题与后续优先级](docs/handoff/BETA13_KNOWN_ISSUES.zh-CN.md)：哪些已实现，哪些只是实验，哪些仍失败。
- [当前包与验证身份](docs/interop/052_BETA14_DELIVERY.json)：双语Core-only OTA、组件哈希及实际检查范围；[Beta13交付](docs/interop/052_BETA13_DELIVERY.json)保留为运行库/依赖和回退来源。
- [正式源码与构建结构](docs/maintenance/SOURCE_LAYOUT.md)：#321 折回后的源码入口、外部 carrier 集成与 Beta 13 字节对照。继续开发时直接修改正式源码，无需恢复 Python 补丁链。
- [私有 GitHub 交接草稿](https://github.com/smartLanny/dlss5-nr-before-sr-lab/releases)：两个用户包及 `PRIVATE-DLSS5-DevInputs-20260929-r2.zip` 开发输入。开发输入不得混入公开用户包。

```powershell
gh auth login
gh repo clone smartLanny/dlss5-nr-before-sr-lab dlss5-core -- --branch main
cd dlss5-core
git cat-file -t cd8ba7009d333f7b03c463dc4237a913f9913081
gh release download dev-handoff-20260929-beta13 --repo smartLanny/dlss5-nr-before-sr-lab --dir ..\dlss5-handoff
```

这是私有仓库，需要维护者账号权限。不要用 Manager 公开页面上的旧 Core 代替本分支。下载后按 `SHA256SUMS.txt` 核对附件，再看完整新机说明；普通 Core 开发可以使用已核验的缓存迁移路径，不依赖旧电脑盘符。原始 F8、截图及游戏转储仍按既有规则留在旧机，没有冒充已完整迁移；详细研究报告、验证回执和最新调试映射已整理成本地 `PRIVATE-Beta13-DebugAndReports-20260929.zip`，其上传仍待维护者明确确认；目前不要假定 GitHub 已有该附件。源码与依赖接续不受影响。

给接续 Agent：先读 `AGENTS.md`、本页、`docs/interop/CURRENT_STATUS.md` 和最新#309规划。Beta14增加有界诊断，不是新降噪/显存优化算法；维护者接受的异环Esc表现不能扩大为所有UI预览问题已修复。私有SR及两个空间实验保留手动选择、默认关闭，质量未过门；普通恢复为建议基线。下一产品行为改动用Beta15，先开发侧连续帧证据和有证据的最小修正，再集中给维护者验收，不重复已接受的Esc、旧A/B/C或切层分配测试。

#313 合入后，#308、#316（整条分支已在历史中）与 #318、#319（修复已由 `5934209a` 移植）已关闭，不要重复 cherry-pick。新改动走可审查的任务分支和 PR；main 整合由 Claude 维护会话负责（见 #311），公开发布和 Manager 默认版本由维护者决定。不启动或控制游戏；实机测试由维护者安排。
