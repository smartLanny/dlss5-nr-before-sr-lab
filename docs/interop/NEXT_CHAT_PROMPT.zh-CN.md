# 下次本地或云端任务的接续入口

先读 docs/interop/CURRENT_STATUS.md、远端 PR #133、Issue #80 和最新 Release。不要从旧聊天的 SHA 或未勾选清单重新开发；源码、构建和游戏测试分别以实际 Git 祖先、manifest、validation 和游戏记录为准。

唯一持续兼容分支 codex/multiapi-045-integration，目标 release/0.4.5-test。本轮临时 codex/045-unified-candidate-20260907 用于整合；颜色维护 PR #137、兼容 dev2 #136 的真实历史已保存。原先两个云端对话已经停止，无需唤醒、发送接续任务或重复恢复附件。

维护者最新要求：推出含兼容增强、UI整理及确认BUG修复的新OTA预发布版，写清玩家更新说明，能做时用具备computer-use权限的本机5.6-sol medium测试2077。直接启动 C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64，不经过Steam。基础测试与详细验收分开记录，未观察不宣称通过。

交付后等待维护者进一步开发指示。不要自动推进旧MC0–MC6全队列，不发布稳定版，不移动历史tag或覆盖已有Release。不关闭未经游戏验证的Issue。

收到新的开发指示后，在独立worktree核验远端HEAD和所有现有草稿。Feeder已有独立已push恢复分支codex/045-feeder-recovery，完整源码可编译但官方NGX库的MSVC运行库链接/同帧游戏链未验收，不重新做整套接口。剩余边界包括原生Vulkan、Legacy、线性深度/FFX RR、部分后置格式和真实引擎R控制。

保留原生回退、真实Reset、同帧资源/命令状态和唯一NR所有者。不吞访问异常、不CPU假报GPU完成、不隐藏旁路。异环SR后第二次状态检查、开场交替和低分后置继续依据已有资料与真实F8单TXT；没有blend四值时标缺失，不填造。玩家只需原场景F6/前后置/F8，视觉异常方便时F7，不要求重交已有资料。
