# Perf12：辅助层依赖退休与可解释的纹理账目

研究目标 next/0.5。增量基线为 Perf11 #297，`f7ca0d9644e56b41114719877b136d1a8af2bdf3`；保留累计 Perf10 #292 和 Perf11 的独立历史。仅修复已通过实际旧收集器复现的依赖退休缺口，不把用户报告的全部显存增长、雨天/FG/切镜卡顿归为该机制。

## 新录制与既有修复

D21new原始manifest/ETL/F8同PID2592，实际加载Perf10 bdc9cf8c，Core SHA256 `321c65c4a9566842888ff46668947336dabe43ed3e078a4e4491befbe06076f5`。新F8 SHA256 `827c5b69aefeb7dd1abf772d76f2b1973eb8d671ece77a2cca537ba607fe7337`，ETL SHA256 `6bc0b6bd445e47040ab701deb71ace2c64d1ee9763be6e4bdf9f514860111bc3`。没有逐帧CSV、物理/进程VRAM时间序列或匹配D13原始ETL。

同进程日志确认先开启、23:31:05.304 F6关闭、23:31:12.443再开启；23:31:26.479最后关闭晚于ETL的23:31:20.885结尾。最终F8在23:31:37.316采集，不能用requested.enabled=0标记整个ETL。两次开启间主实例1不变，辅助实例2→3；旧辅助在23:31:12.730附近完成精确fence后释放，不是已有证据证明永远泄漏。

#297已实现同Create合同只重置辅助历史、不重建整套辅助实例。新附件运行的是Perf10而非Perf11，不能把本次数据当作Perf11验收。该复用有望减少无用创建和新旧实例同时存在的峰值，但不减少模型正常运行所需的全部历史和帧槽。

旧PID23356的长会话F8记录 `dual.textureBytes=424148992`，新PID2592最终OFF记录106037248，数值相差4倍，但两个都是Perf10、不同进程/时刻。该字段是global auxiliary ledger，随已分配槽数变化、包含退休资源但不含opaque NGX，也不包含全部主层资源。不能写成“已减少75%显存”，不能从它单独证明泄漏。

## 其他PR已做到哪一步

#279 uniform3实际把辅助计划改为1:1并允许1–5层独立调参。同尺寸路径可直接引用上层结果，不必创建该降采样路径的输入/valid/delta，但统一尺寸本身也会增大原本50%辅助层的工作量。它不是保持所有设置不变的一项独立显存减半补丁。该PR明确仍未验证游戏总显存，完整buffer reuse、预算拒绝保留旧配置和更细的Present退休仍未完成。#289颜色/边缘线继承它，纯性能线并未完整包含它。

已核对现有open/closed议题与远端分支，没有找到独立已完成、已合入当前性能版的通用显存回收PR。已有#113/#275/#9等研究预算任务不等于运行时显存已经优化完毕。

## 源码缺口及本轮修复

`FindOrCreateContext`在主实例尺寸、导引存储、颜色合同或模型创建调参变化时，将主实例设为retired。旧辅助的父实例不匹配，只会在之后运行对应 `PrepareOne` 时才退休。如果切换后走单层、不再调用旧路线，或父实例在子层准备前已被收集，旧辅助仍可留在活动表中，保留NGX句柄、主层资源引用和辅助纹理。

原 `CollectRetiredContextsLocked`只处理已经retired的对象，不主动传播父实例退休。新函数在移动主实例前，按完整 `carrier handle + feature generation + mode + parent instance` 将相关辅助层标记retired。全局instance非零，父必须为layer0且真实retired，不是仅仅F6 OFF或history reset。无需扫描任何GPU资源内容、无需AddRef/Release或新分配。

实际销毁仍由每个辅助自己的 `CanDestroyContextLocked`、全部slot/binding合同、recording引用、pending token、queue fence/Signal、未知生命周期及quarantine决定。父已完成不能代替子完成；UINT64_MAX设备移除不能算完成。资源移动后slot owner继续重绑，NGX Release仍在锁外、原API串行锁下执行。原NGX失败隔离不改。

此修复不会在F6关闭时清空正常可复用模型，不会共享两个层的时域历史，也不改变SR/RR/FG、分辨率/subrect、shader、模型参数、jitter/MV/depth/exposure或画质工作量。代价为收集阶段额外扫描最多16个主候选，只有实际退休主实例才检查最多16个依赖候选；无逐帧资源查询或GPU等待。

## F8可见性

新增 `contextMemory.contexts`、`contextMemory.auxSlots`、`contextMemory.auxTrackedBytes`、`contextMemory.parentRetirements`。在原链锁中读取固定表，区分活动、退休但等待自身完成、已排队销毁。quarantine计数为这些表内的子集；销毁在锁外进行，global辅助字节与两个表相加可能不同时刻一致或包含未列出的隔离依赖，不能把差值叫内存泄漏。

这些字段不调用GetDesc/NGX、不触发模型创建、不查询GPU工作完成，仅用于分类已有账目。**它们不是进程VRAM，也不是整个显卡占用。** 全设备监控、DXGI进程CurrentUsage/Budget和本插件资源账目应分开看。

## 直接回归

`validate-aux-parent-retirement.py`抽取冻结Perf11和当前实际collector及原 `CanDestroyContextLocked/CanMutateSlotLocked/Is*FenceCompleteLocked`，只将COM/fence服务替换为可控CPU对象。

旧复现：root scheduled=1、auxiliary scheduled=0、orphan live=1，按预期退出3。新源root/auxiliary各1、orphan live=0，退出0。

覆盖10类辅助自己的释放阻碍（录制引用、未提交token、未Signal、未完成/未知fence、slot引用/Signal/fence、上下文/slot绑定）、两类opaque quarantine、240种父子数组位置、200次四辅助层级联、满待销毁队列、不同父/代次/路线和主层身份、live-parent/F6保持，以及F8活动/退休/待销毁/未列出差额语义。不能用这些CPU测试声称真实NVIDIA显存已下降。

Perf11的128历史代次复用/Reset和14类存储寿命反例继续通过。完整双语Windows、原生生命周期/WARP及实际打包以Windows交付文档和真实回执为准。

## 下一次实际验收

沿用同机器RR后置双NR、3440×1440+1720×720、强度1.5/局部1/结构1.2、同模型和FG。先让双层稳定，再同进程F6关开关，确认Perf11复用仍有效、Reset真实提交且不反复Create。

另一段用于本修复：从双层稳定状态做一次真实会改变主实例的NR尺寸或Create调参更改，再转单层/关闭，等待正常提交与fence完成，保存F8及外部进程VRAM/预算记录。旧父子都应进入退休流程，不能因不再调用旧辅助路线而无限保持活动。不要用固定等待帧数强行释放，也不要用降低层数后的FPS作为同画质优化成绩。NGX/驱动缓存可能不立即把进程占用退回初始值，需同时看实例/退休与预算证据。

两段合并为一次有事件标记的短测试即可，不重录旧四版本矩阵。出现持续旁路、无效果、旧历史拖影、闪烁或崩溃应回滚并上报同次F8、Core日志和构建哈希。#277/#296保持开放；本轮未宣称整个最新D21对早期D21的显存差距都已解决。

公开API依据：Microsoft D3D12 CPU/GPU synchronization要求资源保留到GPU完成；DXGI_QUERY_VIDEO_MEMORY_INFO的CurrentUsage是进程值，超过Budget可能引起分页/卡顿。不能据此反推本次雨天必然发生显存超预算。
