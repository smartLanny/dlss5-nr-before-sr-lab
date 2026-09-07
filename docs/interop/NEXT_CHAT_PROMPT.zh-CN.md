# 下次本地或云端任务的接续入口

维护者于2026-09-07授权将最新0.4.5 fix2/dev3兼容实现收敛到main，并精简F8和修复稳定性问题。本轮候选dev4，main是唯一持续开发入口；不要继续往旧release或兼容分支独立积累修复。

先读取远端main、docs/interop/CURRENT_STATUS.md与MAIN_CONSOLIDATION.md。核对source-manifest.json和validation.json的精确源码身份；历史dev3测试、最新文档HEAD与dev4实机验收分别记录，不能互相替代。dev3已发布Tag b03f474ba5127ab057fbb5e93708c47eb9dbb866不覆盖。

保护所有原worktree和未提交文件，从origin/main建立短期工作分支。已整合维护分支由收敛记录列出；0.5、Feeder、Manager等未完成独立实验不按版本号盲目合并。Feeder的恢复分支codex/045-feeder-recovery已有真实源码，未验收完整同帧游戏链，不重新恢复旧草稿。

保留原生回退、真实Reset、同帧资源/命令状态和唯一NR所有者。不吞访问异常、不CPU假报GPU完成、不隐藏旁路。异环、低分后置、HDR/运动/长期稳定与其他游戏以各Issue真实资料验收，不因main合并或Windows构建而关闭。玩家回传原场景F6/前后置/F8，视觉异常方便时F7；已有资料不要求重交。

本轮交付后等待维护者下一步指示，不自动推进旧MC0–MC6路线图，不发布稳定版、不覆盖旧Release。
