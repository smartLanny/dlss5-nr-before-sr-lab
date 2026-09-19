# 2026-09-19 性能候选与工作树收敛记录

维护者明确授权清理过时Issues、PR和工作树，并要求可交给其他Agent继续。
本轮不改运行时代码，不自动合并main/stable，不删除远端或本地分支，不发布Release。

## 在线盘点

开始时：24个open PR、91个open Issues、167个远端分支；main为
`dae86e9898399c20e52ada54a5ec4350da4d4fa1`。新建本次隔离维护工作树后共有31个
注册worktree。盘点包含脏文件、未跟踪/忽略文件、HEAD、分支、代次包含关系及
进程引用；不是按文件夹日期直接删除。

## PR整理：只留一个性能代码入口

以下PR的当前HEAD均用`git merge-base --is-ancestor`验证已包含在
Perf10 #292 / `0b01a8e5f6d32cdbc57d7c0c4a7d106a9bdc442a`：

| 关闭为被后继替代的PR | 头提交 |
|---|---|
| #280 Lock2 | `9039018fbd70702c2d3ad3181e01a3fc1fd3d71e` |
| #282 Perf3 | `c144d7cb8553f19972ab4b20a88b13dea1b69b94` |
| #283 Perf4 | `ea9e0b896e6d386b719cddb916b66bc99c43768b` |
| #284 Perf5 | `cafb42215b02f0043cd93a16e4077f0d7fd26963` |
| #286 Perf6 | `8baa048cade9bb10b0108c77ad4e014717b49377` |
| #287 Perf7 | `a3ea8f9cbf3e06662e622852dc11581973798d18` |
| #290 Perf8 | `43081138a6f53c487fe1204bfddb89f0ff1933a9` |
| #291 Perf9 | `a93f6d00a6820c2013644ea5aaa415baafc91883` |

关闭前再次核对HEAD未被其他Agent推进，逐单写明继任#292以及“并非已合并/
并非BUG修复完成”。保留所有分支、历史评论与原始版本身份。#292继续草稿和
待游戏验收；独立功能PR没有按时间清理。独立Perf8R没有完整包含关系，保留。

## Issue整理

- #228与#229均是DS2 typed R8_UNORM曝光被float-only reader拒绝。读取双方正文
  和原评论后，将简版#228关闭为duplicate/not_planned，详细#229仍open。
  旧报告、#2资源语义关联和待验证要求以互相链接的评论保留；未宣称问题已修好。
- #277、#272、#273的游戏/机器/路径不同，不作为重复BUG关闭，继续
  `open / partial-fix / needs-retest / fix-in-pr`，统一代码入口#292。
- 新#293专门跟踪首次NR模块加载/Feature18创建延迟，区分冷启动和热F6。
- 新#294跟踪与colour4-edge1/uniform3/D22的功能集成及验证。

尚未证实重复或修复的旧兼容性、颜色、RR/FG、Manager等Issue保持原状态。
“版本旧”不是关闭一个实际仍未验收问题的依据。

## 工作树整理与恢复

已完整归档并正常移除10个工作树：Perf3、Perf4、Perf5、Perf6、Perf8共5个，
以及5个无改动、无忽略文件且已被累计性能头包含的重复detached临时工作树。

每份归档包含全部普通工作文件（含被Git忽略的测试/分析输出），不包含旧
`.git`指针。压缩后对每个文件解压计算SHA256，再对仍存在的原文件重新哈希；
文件集合/mtime/大小必须稳定，HEAD不得变化，没有dirty/untracked、Git锁或
指向该树的进程引用，才执行**不带force**的`git worktree remove`。

另创建并验证`performance-through-perf10.bundle`，保留累计Git历史的独立
恢复入口。原Git分支未删除，归档未上传公共仓库。

原目录普通文件逻辑总量 **9,365,709,515字节（约9.37GB）**；归档、清单和
Git bundle合计 **176,670,603字节（约177MB）**。这是文件逻辑大小口径；真实
物理磁盘回收还受文件系统压缩、稀疏文件与后台写入影响，不冒充精确磁盘指标。

恢复方法：从清理回执取得原路径和HEAD，运行

```text
git bundle verify <archive>/performance-through-perf10.bundle
git worktree add --detach <restored-path> <saved-head>
```

然后将对应ZIP解压到该新工作树，保留新生成的`.git`，按归档manifest核验文件。
需要继续原分支时先确认该分支没有被其他工作树占用。不要把旧`.git`路径写回。

### 特意保留的内容

- Perf7检测到重解析点/链接，未继续归档或移除，避免误处理链接目标。
- 所有dirty工作树，包括旧Cursor构建、临时诊断内容。
- 最新Perf9、Perf10及实际交付包、原始录制目录。
- 未合并Perf8R独立实验、colour/五层/D22/私有SR/残差等独立工作。
- 依赖工具、主product工作区、原始ETL/CSV/F8与已交付候选均未删除。

## 合并与下一步

性能来源只读#292；新功能入口与真正文本/语义冲突已通过不修改工作树的
`git merge-tree --write-tree`预演记录在
[性能集成交接](PERFORMANCE_INTEGRATION_HANDOFF_20260919.md)。
本轮没有声称组合源码已编译或游戏实测。工作树减少后，旧绝对路径不可再当
构建输入；应从固定Git源码和独立deliveries/工具目录取依赖。
