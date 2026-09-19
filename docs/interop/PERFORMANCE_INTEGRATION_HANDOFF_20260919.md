# 性能主线交接：累计 Perf10、实测边界与功能集成

更新日期：2026-09-19。此文是当前性能工作入口，不替代每次在线读取。

## 1. 唯一累计代码来源与状态

| 对象 | 当前核验值 |
|---|---|
| 性能累计 PR | [#292](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/292) |
| 性能分支 | `codex/d21-recording-traffic-perf10-20260919` |
| 本次冻结 HEAD | `0b01a8e5f6d32cdbc57d7c0c4a7d106a9bdc442a` |
| 实际编译源码 | `bdc9cf8c936227c4bc73bcfa353017e6fffdb312` |
| 最新候选 | `beta0.5-dline21-perf10` |
| 中文 Core SHA256 | `321c65c4a9566842888ff46668947336dabe43ed3e078a4e4491befbe06076f5` |
| 配套 nrchain SHA256 | `1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2` |
| 中文 OTA SHA256 | `7c2a30edecf4dca4604b46d22b3380c76368974bc59b47ff189a5ad84dce4528` |
| 研究集成目标 | `next/0.5`，不是 main/stable |
| source / Windows compile / WARP | 已验证，详见各代实际回执 |
| hosted CI | 付款/额度导致未启动，不算通过 |
| Perf10 游戏、NVIDIA 推理、弱 CPU、长期兼容性 | 尚待维护者验证 |
| D3D12 调试层 | 不可用，GPU 读回/负控通过但 InfoQueue 未验证 |

Perf10 包含 Lock2 和 Perf3–9 的全部祖先。旧 PR #280/#282/#283/#284/#286/
#287/#290/#291 已于本次通过祖先校验后关闭为被后继候选替代，不表示已合并
或问题已修复。分支、评论、历史二进制和原始采样保留。不要把八个旧 PR 再
逐个 cherry-pick；也不要只拿 `bdc9cf8c` 最后两文件改动而遗漏之前整条链。

## 2. 到底做了什么

| 阶段 | 修复机制 | 集成时必须保留 |
|---|---|---|
| D21 lockperf1 / Lock2 | registry shared + slot exclusive；根参数有效字段写入；模板外共享的有界列表提示 | 锁顺序、epoch、token、readiness、失败Reset及对象退休；不能裸指针绕过寿命 |
| Perf3–6 | 描述符范围候选、范围规范化/排序、精确字节并集、有界槽提示；F8 rangeQueries/slotQueries | 同类型才合并，不能填stride空隙；pending/unknown/overlap、heap代次与完整键一致 |
| Perf4 来源失败路径 | 满表失败键缓存；重复Unknown失效的幂等消重 | 仍拒绝无法登记的批次；新发布/提交/退休重新置脏；不洗掉FG/generated taint |
| Perf7 | 精确资源状态索引、Barrier内身份复用、Core/observer/设备查询联动 | 同一调用内资源借用、原source_lock、split/子资源/alias失效和纯UAV身份副作用 |
| Perf8 | 成员索引及局部探测簇删除替代局部Reset整表Rebuild；token位置提示；可重入范围存储与按形状排序 | 固定容量、first-free、无淘汰、完整recording/resource键、嵌套scratch所有权、分配失败回退 |
| Perf9 | 批次头与事件载荷分离，pending位图；四路快照提示 | 有效事件数/版本、失败与重放提交、完整别名作用域、淘汰提示不等于删除状态 |
| Perf10 | Reset只清全部类型/值/mask；小批次原循环/大顶点批次复制；精确无工作提交早退 | 常量按kind/mask读取，旧载荷不能复活；无pending证明在原锁内，有工作不跳Signal |

关键生产文件包括 `nr_command_state.inl`、`nr_descriptor_provenance.h`、
`nr_descriptor_provenance_runtime.inl`、`nr_descriptor_scratch.h`（作为依赖使用）、
`nr_source_state_table.inl`、`nr_source_frame.hpp`、`nr_source_frame_runtime.inl`、
`nr_source_observer.inl`、`nr_post_boundary_native.inl`、`nr_feedback_capture.inl`。
完整来源以提交差异为准，不能把这个列表当作应整文件覆盖的白名单。

这条后续性能链并未改变 NR 模型、层数、分辨率、shader算法、色彩强度或
NR/SR/RR/FG 路由。整合到画质线后应保留画质线的意图，不能用旧的纯性能
文件覆盖它的 shader/参数；新的组合也不能继续冒用纯性能包的版本/哈希。

## 3. 可诚实声称的进步与不能声称的进步

### 真实录制中的证据

- 早期同轮原版 D21→Lock2 三阶段平均 FPS 约高4.6%–5.3%，ON已接近该轮
  D13/Reno。后台和GPU耗时仍有差异，不能宣布普遍提升5%。
- Perf3→Perf4 的 ON `Lineage::Add+BatchFor` 编译区间self合计约
  11.62→4.26 CPU ms/s，减少约63.4%。这是较明确的局部命中，不是FPS降耗同比。
- Perf5/6 F8证明有序范围与槽缓存进入真实游戏，不是理想微基准没被使用；
  后续继续发现Reset/退休/提交成本，说明需要整条维护路径一起优化。
- 最新重新核对、相同NR→SR/2228×1253/单层2-2-2/UI校正开启的Perf8/9：

| 阶段 | Perf8 FPS | Perf9 FPS | Perf8 / Perf9 插件self CPU ms/s |
|---|---:|---:|---|
| 首次 OFF | 81.09 | 82.56 | 36.87 / 35.02 |
| ON | 62.05 | 62.97 | 97.33 / 90.00 |
| 再次 OFF | 80.73 | 82.87 | 74.13 / 62.21 |

最新样本ON约+1.49%，插件self约-7.53%；再次OFF插件self约-16.07%。
仍只有一次录制，Perf8安全扫描与Perf9 PowerShell背景不一致；没有精确QPC
合窗、同期物理GPU时钟/功耗和多次随机交错。因此不能把所有增幅都归给代码，
更不能将历代百分比相加、相乘得到“总提升”。不同路线、NR工作尺寸、UI校正
和显卡条件的旧数据不能作为横向分母。

### 尚未解决

Perf9再次OFF的插件self仍为62.21，而首次OFF为35.02 CPU ms/s；关闭态
恢复FPS不等于恢复全部维护成本。原始D13与D21的弱CPU差距尚未在最新包上
完成严格验收。Perf10本身尚无本轮新增游戏记录，不能称最新包已超越D13或
与Reno具有相同画质/工作量。没有用减少NR工作或安全旁路换跑分。

## 4. 首次开启卡顿：独立任务 #293

[首次NR冷启动任务 #293](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/293)
与 [稳态/残留成本 #277](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/277)
分开。维护者观察后续F6热切换不明显卡，不能笼统称每次F6都会卡。

实际 `CreateSignedNrFeature`（Perf10 `nr_before_sr.cpp` 约10043行）先写
创建参数日志，再取得 `g_nr_api_lock`，调用 bridge_create/signed_create。
创建日志间隔包括锁等待、包装和Create CPU墙钟，不等于GPU推理时间。
最新两份首次长帧约1777/613ms；Feature18创建日志间隔约1367/131ms。
此前没有改创建流程，不能把缓存历史差异当成已修复冷启动。

可行的下一批分为“减少重复准备”与“避免把不可避免的准备堵在按键后的游戏
帧上”。先细分模块加载、身份/参数、CPU资源准备、API锁等待、Create、必要
队列完成、首次Evaluate、首个成功增强Present，统一QPC计时。可以研究明确
的加载阶段预备、受控实例复用和用户选择的预备状态，但默认OFF不能偷偷
推理/改变显存承诺。预备只是转移等待时机时要如实说明，不叫计算消失。

不可直接让后台线程操作游戏正在录制的command list。若研究独立队列，
必须验证运行库支持、专有allocator/list、fence、设备丢失/退出和失败Create
隔离。当前失败Create返回handle的quarantine/latch不得删除。先保留同步
安全回退；不能承诺未知运行库内部创建能被随意分帧。

公开生命周期依据：
- Microsoft [Recording command lists](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles)
- Microsoft [Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-reset)

## 5. 与最新功能版合并：已做预演，不是已完成组合包

执行跟踪：[集成任务 #294](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/294)。

| 功能输入 | 本次线上固定头 | 与Perf10合并的文本冲突 |
|---|---|---|
| uniform3 #279 | `58ce637c1fbb03d0e7d93b90d26b3e4bce7b03df` | build.ps1、prepare-multi-api-beta.py、nr_source_frame_test.cpp |
| colour4-edge1 #289 | `8c469637409c6ed4ee30a40e2d6b62e295625f91` | 同上 |
| D22 Core entry | `f0ca84c64a76c3b3e9fe02f83b424900a86c2407` | prepare-multi-api-beta.py、tests/multi_api/test_integration.py |

`git merge-tree --write-tree` 对每一对输入实际执行，没有改功能工作树。
colour4-edge1包含uniform3及#281→#285→#288的祖先；从最新colour4头接续时
不要把这些再重复合并。本机名为uniform的工作树为6a6980ae，而线上PR279
已到58ce637c，必须以在线头而非目录名作为身份。

Perf10与colour4的共同祖先为cbfb3391。双方共同修改五个运行时文件：
`nr_before_sr.cpp`、`nr_feedback_capture.inl`、`nr_source_frame.hpp`、
`nr_source_frame_runtime.inl`、`nr_source_transport.inl`。文本能自动合并
不表示语义正确，特别要审查新版结构、槽寿命/重放、guide pending、NR路由、
颜色重建、每层尺寸与F8字段是否都保留。

推荐流程：从功能owner确认的最新功能HEAD建独立集成worktree→三方合入一次
累计Perf10→需要时加入D22独立输入→解决生成器/测试语义冲突→新版本身份→
完整生成、双语编译、WARP及功能/性能对照→让维护者实测。不可用ours/theirs
整文件取胜；不可删除失败测试、保留旧source pins或直接复制旧DLL。

生成器版本串、所有固定blob、构建入口、依赖列表、当前结构测试、source
manifest和最终Core/companion哈希是一个事务。新组合F+P若仅加CPU优化，
shader应与同一功能基线F一致，而非强迫与纯性能旧包相同；功能本身改变
shader时应明确列出。断言被新版语义取代时重写真实验证，不能静默跳过。

最小交付至少保留功能版F与同功能+性能版F+P。NR前后置各自同配置对照，
单层/多层、99/100%、UI校正、RGB/颜色跟随、人脸/边缘、RR/FG/Present、
Reset/resize/退出和旁路反馈都要验收；无原生DLSS/DX11作为独立兼容补充。
非同画质、不同NR实际工作尺寸的数据不用于性能胜负。

## 6. 项目管理结论

旧性能PR只做行政收敛，未合并主线；#277/#272/#273保留各自游戏/硬件证据，
不把不同样本都当重复BUG。重复DS2曝光入口#228关闭为重复，详细主单#229
继续开放，未宣称修好。冷启动和集成分别用#293/#294管理。

10个确定闲置的旧工作树已完整压缩归档并逐文件重新解压哈希，再正常
`git worktree remove`；没有force删除或删分支。Perf7有重解析点，保持原样；
未提交、独立Perf8R、最新Perf9/10和所有画质/功能线保留。详细恢复与保留
清单见 [清理记录](PERFORMANCE_CLEANUP_20260919.md)。

可直接转发的执行提示见 [Agent交接](PERF10_AGENT_PROMPT_20260919.md)。
