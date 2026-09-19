# 给集成 Agent 的执行交接

把下面整段交给负责新版本的 Agent。它是一项实际执行任务，不是要求再次给教程。

---

维护仓库 `smartLanny/dlss5-nr-before-sr-lab`。请将累计性能优化与我正在开发的最新功能版本整合，保留功能，并推进到能交给我测试的组合候选。

先在线读取 main、open PR/Issues、最新功能分支和当前工作树，不要依赖旧聊天或本地目录名。读取本仓库 `docs/interop/PERFORMANCE_INTEGRATION_HANDOFF_20260919.md`，以及 #294 集成任务和 #293 首次启用任务。文档不在你的功能分支时，从文档PR或该提交读取，不要误判不存在。

性能唯一输入为 #292 / `codex/d21-recording-traffic-perf10-20260919`，本次冻结 HEAD `0b01a8e5f6d32cdbc57d7c0c4a7d106a9bdc442a`，实际已编译来源 `bdc9cf8c936227c4bc73bcfa353017e6fffdb312`。它已包含 Lock2 至 Perf9。不要逐个重复 cherry-pick 已关闭的旧性能PR，也不要只拿最后Perf10的两个文件改动而漏掉祖先。以后若HEAD推进，先核对新增差异与二进制身份。

本次最新画质入口为 #289 `codex/d21-colour4-edge-recovery-20260919`，`8c469637409c6ed4ee30a40e2d6b62e295625f91`，已包含uniform3及#281/#285/#288祖先。D22入口修复为 `codex/d22-core-entry-before-create-20260917` / `f0ca84c64a76c3b3e9fe02f83b424900a86c2407`，仍是独立输入。以你实际读取到的最新功能头为准；不要覆盖别的Agent工作树，尤其本地uniform目录当前可能落后线上。

从最新功能头建独立集成分支，使用共同祖先做三方合并一次累计性能线，再按需要接D22。保留颜色优先/RGB恢复/边缘/人脸/多层/Present等功能，保留性能线的寿命锁、epoch、完整键、pending/unknown、Reset/回放和失败隔离。禁止整文件ours/theirs覆盖、删失败测试凑绿或把安全旁路当性能优化。

已做的两两合并预演：Perf10与colour4/uniform冲突在 `scripts/build.ps1`、`scripts/prepare-multi-api-beta.py`、`tests/nr_source_frame_test.cpp`。另有五个双方改动的运行时文件虽可文本自动合并，仍须审查：`nr_before_sr.cpp`、`nr_feedback_capture.inl`、`nr_source_frame.hpp`、`nr_source_frame_runtime.inl`、`nr_source_transport.inl`。D22合入时还要看生成器和 `tests/multi_api/test_integration.py`。这不是已经完成的组合编译或游戏验证。

生成器的版本/source pins、依赖列表、完整源码生成、构建脚本、真实新结构的测试要一起更新。新组合用新版本标识/commit/hash，不复用纯Perf10旧包身份。做完整双语Core编译、继承性能/颜色/多层/状态恢复/来源传输和GPU读回/负控。调试层不可用单列，CI账单未启动不算通过；不能只编一个源文件就交付。

至少保留功能基线F和功能+性能F+P两份同设置对照；性能改动若没有改变画质，F+P shader应对应F而非旧纯性能包。分别测试NR→SR与SR→NR、单层与多层、99/100%、UI校正/颜色跟随/人脸/边缘、RR/FG/Present、Reset/resize/退出。所有性能百分比仅来自同镜头同参数同实际NR尺寸；采样CPU时间不是阻塞时间，不把跨窗口CPU率除以FPS伪装精确每帧成本。

首次启用秒级停顿尚未解决，独立按 #293 记录加载、API锁等待、Create、必要提交完成和首个NR成功帧。后续F6热切换通常不明显卡，不要误写为每次F6必卡。未经实际运行库/队列/寿命验证，不把游戏command list移到后台线程；预备移到加载阶段要明确说明，不自动在OFF推理。

完成后提交一个组合PR并在 #294/#292 回复，交付中文Core+匹配nrchain及manifest/哈希，保留原INI/模型/桥接器。不要自动合并main/stable或发布Release。#277/#272/#273在新组合实际验收前保持开放。持续旁路、无效果、闪烁或崩溃需回滚并要求F8、Core日志和身份反馈。不要停在“建议下一步修某函数”，有明确证据就将关联修复、验证和候选一起做完，再给我最小下一轮测试。

---

## 清理后的本地注意点

旧Perf3/4/5/6/8工作树和5个无改动重复临时工作树已归档移除；Git分支均保留，随时可在新路径恢复。不要依赖旧工作树的绝对路径作为构建输入。
最新Perf9/Perf10、独立Perf8R、dirty和画质/功能工作树保持原样，Perf7因重解析点未清理。
原始ETL、已交付包和依赖工具目录未删除；归档记录含逐文件哈希和恢复方法。不要把“目录不再注册”当成代码或证据丢失。
