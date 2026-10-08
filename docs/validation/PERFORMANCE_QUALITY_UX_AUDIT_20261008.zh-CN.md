# 性能、画质与操作逻辑专项审查（2026-10-08）

## 结论与执行边界

最值得推进的不是再叠一个模型，而是：**先让设置切换后可靠恢复，再测清每份成本，保留真正有收益的画面改动，最后让用户能理解和控制这些取舍。** 本报告是现有 #309/#311/#333 工作的补充，不新建另一条整合主线或另一个总任务。

本轮在线读取 main、开放 Issues/PR、分支和关键源码；独立运行了一个描述符堆预检查的 CPU 决策模型。没有可调用的 subagent 接口，Windows MCP 两次返回 OAuth 503，Mac MCP 连接失败，**实际没有启动子代理，也没有 Windows 编译、WARP、NGX 或游戏测试**。下文并行分工是交给有执行能力的本地协调器的工作包，不是已执行回执。

不修改生产 C++/shader、保存设置、默认值、运行库、已部署 Beta16、冻结分支、Manager、发布或 main。Claude 继续负责整合。与 #311 的 v17 一致：owner 暂不补测，开发侧先完成已有受控工作的判断，再统一短测。

## 1. 冻结审查身份，避免重复开发

| 对象 | 本轮核对身份与实际边界 |
|---|---|
| main | `024f9cc32778c1056db364d76d087ee257da6502`，Beta14；不是聊天里的 D21，也不是本地组合 Beta16 |
| #337 | Beta14 基线/交付及过时入口文档修正；不重复修改共享状态页 |
| #338 | `ad37757cc5eed7b4e62bf63e445d28f1d4677837`；逐通道中性保护、有界采集、快捷键已经有实现 |
| #340 | `9a81200d5b56e7016d0b2dcfe1e18afe183b2af7`；显式依赖 #338，移除私有平面固定 768 MiB 拒绝、改善路线状态 |
| #341 | `f9ef953faad0a31d280d27adbbe411433a352a47`；原生命令列表死亡通知配对退休；独立 canonical 有回执，玩家实际 holder 尚未证实 |
| #342 | `78632125c7a1d8fef98cd7ff7e074283bc4e25ad`；两 RR/3D 接缝研究；产品仍不是新两 RR 路线 |
| #343 | `3e9659abb326ca779e5f88cb8ba1f8c1f1daa18a`；显存与同身份时间观察；含它的完整组合 canonical 仍未通过 |

main 的 `CURRENT_STATUS.md` 仍写 Beta13，不能把这个旧标题当当前二进制身份。已有 #337 处理文档更新。#340 的 owner 明确授权覆盖了较早计划里的固定 768 MiB 限制，不能把旧限制重新当硬约束。#342/#311 提及的本地组合 Beta16 与各独立 PR 的二进制、构建回执必须分开。

来源：[main ref](https://github.com/smartLanny/dlss5-nr-before-sr-lab/tree/024f9cc32778c1056db364d76d087ee257da6502)、[AGENTS](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/024f9cc32778c1056db364d76d087ee257da6502/AGENTS.md)、[#311 v17](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/311#issuecomment-6055528864)、[#338](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/338)、[#340](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/340)、[#341](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/341)、[#342](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/342)、[#343](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/343)。

## 2. 源码核对：四类旧优化已经存在

### 2.1 热路径不是旧 D21 的样子

`src/nr_command_state.inl` 已经做到：

- `CommandStateSetValue` 只改 kind/value/mask，不清 256-byte constants payload；Reset/Release 也只失效对应有效性字段。
- `CommandStateLookupCache` 是共享于所有 setter 实例的四项 TLS 缓存，不再是单项缓存；保留 epoch 和负缓存。
- registry-shared + 独立 slot mutation lock 已替代所有 setter 共抢全局写锁；不能再宣传“准备引入分锁”。

仍可研究的具体位置：`CommandStateSetHeaps` 在识别“完全相同的已知堆集合”之前仍调用 `GetDesc`；`CopyNativeCommandState` 仍整块复制 `arguments[2][64]`。这些是**成本候选**，不是已经证明的游戏瓶颈。

来源：[状态模型与 setter，170–350](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/024f9cc32778c1056db364d76d087ee257da6502/src/nr_command_state.inl#L170-L350)、[分锁与缓存，590–660](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/024f9cc32778c1056db364d76d087ee257da6502/src/nr_command_state.inl#L590-L660)。

### 2.2 显存池也已先尝试安全复用

`FindOrCreateSlot` 在还有 empty slot 时，已经先找 `OldestReusable`，验证 `CanMutateSlotLocked` 和格式/绑定合同，然后复用自有纹理；拒绝时才走旧分配路径。旧 Issue 中“只增长到八槽”的描述不能直接当最新源码事实。

`RetireForCompaction` 已对明确缩层/换路线标记旧闭包退休，并明确让普通 F6 保持热状态。剩余问题应聚焦**闲置路线、真实在途高水位、预算压力和已完成槽的适度收缩**，不是再写一份基本复用器。

来源：[FindOrCreateSlot](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/024f9cc32778c1056db364d76d087ee257da6502/src/nr_before_sr.cpp#L9800-L9945)、[驻留策略](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/024f9cc32778c1056db364d76d087ee257da6502/src/nr_residency_policy.h)。

## 3. P0：先完成恢复能力与组合验证

#341 的方向值得优先审查：private-data marker 被替换、ReShade wrapper 销毁，与原生命令列表真实死亡不是同一事件；GPU fence 完成也不等于可重放 recording 引用已经归还。该 PR 的实际死亡通知只置位并释放自有小对象引用，collector 再走原有退休/锁外释放，不在销毁回调里调用模型或访问 dying native 对象。

必须保留的验收边界：真实 Create/notifier 覆盖不足时，#341 会拒绝新 NR。它可能提高安全性，也可能扩大某些 late-attach 游戏的旁路范围；需要把正常运行、切层/切路线后恢复、早期/晚期观察、wrapper 替换、native Release、不支持通知、FG 共存分别记账。不能将旁路后的高 FPS 当优化，不能为通过宿主而放松 guard。

当前整合关注点是 **#341 + #343 + 冻结 #338/#340 的交叉面**：退休文本容量、F8 writer 锁、小线程栈、采集窗口排除、时间身份和缺失样本。#343 单分支测试或 #341 单分支 canonical 不替代完整组合验收。按现有整合者管理一个组合候选，不让研究代理各自生成“最新包”。

官方依据：[ID3DDestructionNotifier](https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/nn-d3dcommon-id3ddestructionotifier) 明确禁止在回调中访问正在销毁的对象。[#341 回执](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/341)、[#343 回执](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/343)。

## 4. P1：性能优化必须拆成三本账

### 4.1 游戏整帧、插件工作、首次使用分开

建议沿 #333 的现有框架记录三个维度，而非新增一套 profiler：

| 维度 | 必须回答 |
|---|---|
| 游戏体验 | 基础渲染帧时间中位/P95/P99、卡顿、呈现节奏；FG 显示帧率另列 |
| 插件工作 | 实际采用的 NR 层数、逐阶段 GPU 范围、CPU 锁等待/提交空洞、原生 SR/RR 成本变化、旁路比例 |
| 冷热状态 | 首次开关到首个有效结果、Load/初始化/Create/首 Eval、热切换、退休/重建、创建期与稳态显存 |

对照至少区分：无插件、只有宿主注入、NR 从未开启、NR 开过再关闭、NR 稳态开启。F6 off 的热缓存不是泄漏的充分证据，也不是冷关闭成本。最终游戏统计需多个独立运行/交替顺序，不能把同一段相关帧数当独立重复次数。

GPU timestamp 只解释所包围的队列工作，不能把局部 ms 倒数当游戏 FPS，也不能简单相加不同队列的重叠区间。已有查询失败/未完成必须记 missing；真实 0 才能记 0。采集与性能阶段隔离，计时用对应队列频率，必要时校准 CPU/GPU 时间轴。

依据：[D3D12 timing](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing)、[#343 实际限频与采集排除](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/3e9659abb326ca779e5f88cb8ba1f8c1f1daa18a/src/nr_allocation_ledger_runtime.inl)、[#333](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/333)。

### 4.2 两个可窄做的 CPU 候选

**候选 CPU-A：同一描述符堆集合提前返回。** 仅当 descriptor-heap domain 已知、输入个数合法、无空项/重复、输入与当前被强引用保护的 heap 指针集合完全相同，才跳过 `GetDesc`；顺序互换应等价。否则完整保留原验证路径。这不是全局裸指针元数据缓存，也不删除 registry/slot 锁。

本轮附带 [独立 CPU 决策模型](perf_quality_ux_review_20261008/heap_identity_precheck_model.py)：9,576 组简化状态/输入对照通过；两个错误候选（省掉 unknown guard、用集合比较吞掉重复项）被负控识别。重复单堆的模型 GetDesc 计数 1→0，双堆 2→0。**它未编译或执行生产 C++，不证明 COM 生命周期/并发正确，也不证明游戏净加速。** 合成输入分布不能推导真实命中率或百分比提升。

实施门：先记录真实重复绑定占比及耗时；从生产函数抽取实际 C++ 对照，覆盖 Reset/ClearState/未知状态/重复项/交换顺序/元数据失败/地址复用；Windows canonical 与游戏同工作量 A/B 通过后才考虑合并。如果命中少或新增分支抵消收益，就不做。

**候选 CPU-B：状态快照按有效字段复制。** 现有大块 memcpy 可能比稀疏循环更快，不能先改再找理由。先测 snapshot 次数、耗时、活动 root 分布；只在稀疏路径有稳定收益时尝试 headers + 有效 constants spans，保留稠密 fallback、部分 constants 空洞、双 stage 和动态 RTV/DSV 副本。新增指针共享或跨 recording 引用不是这项优化的范围。

### 4.3 首开卡顿不是简单塞进后台线程

把 CPU 可独立准备的配置/字节码/元数据与需要合法 device/list/queue 的模型准备分开。禁止把游戏 command list 交给任意后台线程，也不能在 NR 关闭时偷偷运行模型。首次加载、API 锁等待、Create、首次 GPU Eval 要分别计时。把同样的停顿挪到游戏启动，只能称延迟搬移；必须同时报告启动开销与首次使用改善。入口继续归 #293，不重建一套初始化框架。

## 5. P1：显存以复用、冷却和真实预算管理，不靠猜测清空

#343 提供的是自有分配下界、已采样高水位和有噪声的进程 DXGI 变化。模型内部、某些 owner、PSO/heap 等仍 unknown。自有纹理相加不等于插件完整显存，释放前后进程差也不等于某个 NR 模型的精确占用。

#340 已解决 4K/100% 私有平面约 1051.875 MiB 被固定 768 MiB 上限挡住的问题；它没有消灭这笔占用，也没有证明能容纳模型内部额外分配。继续尊重 owner 的放开决定：不偷偷恢复另一固定 cap。已有 `Budget - CurrentUsage - reserve` 是时点准入，不是未来不超预算的保证。

建议在 #329/#330/#331 内按顺序推进：模型/尺寸冷创建曲线与创建期采样 → 闲置路线退休 → 已完成槽按真实在途需求收缩 → 有滞回/冷却的预算响应。F6 保留常用热模型；显式缩层/换路线与显存压力另走已验证退休闭包。unknown 查询不可解释成无限预算，也不可硬报 0。

不共享层间模型历史；只有证明全部消费者的 queue/fence/recording 已释放后，才允许纹理复用或释放。时间/帧龄只能用于选择候选，不能证明 GPU 完成。自动调整应先做 opt-in，会话有效并展示实际降档理由，不覆盖用户保存比例，不在阈值附近连续重建模型。

依据：[驻留策略](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/024f9cc32778c1056db364d76d087ee257da6502/src/nr_residency_policy.h)、[#343 账本](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/3e9659abb326ca779e5f88cb8ba1f8c1f1daa18a/src/nr_allocation_ledger_runtime.inl)、[#340](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/340)、[DXGI 预算与超预算卡顿](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo)。

## 6. 画质：先证明有净收益，再增加算法

### 6.1 新测试台给出的真实信号

#342 记录的低分 NR→一次 RR、R=1280×720/O=1920×1080、单层、每组 120 预热+120 观察：off/on GPU 中位分别 0.849536/3.327488 ms，稳态进程 local memory 411.65625/849.9296875 MiB。两组中位之差为 2.477952 ms；稳态进程数值差为 438.2734375 MiB。这些差值**不是纯 NR 成本、游戏 FPS 或完整峰值**。

报告中的独立图像复核认为 on 稍平滑/稍软，没有明确整体优势；本轮没有自行复核原图，不能把这个结论扩大到所有游戏。两 RR 同源 3D 现已完成两预热一观察的有效编辑门，但长序列画质/性能尚未证明。99.727322% RGB 发生变化和 FP16 误差界是功能证据，不是“99.7% 画质提升”。

遵守 #311 v17：当前只追加已经授权的两个 on capture/perf 组，不重跑旧四组，不加 renderer/所有权架构。复用历史控制时保留其真实 binary/source 身份和跨运行限制；最终不能把不同身份的臂重标为同 binary 配对测试。

来源：[冻结测试台报告](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/78632125c7a1d8fef98cd7ff7e074283bc4e25ad/docs/validation/DEVELOPMENT_TESTBENCH_RESULTS_20261008.md)。

### 6.2 私有 SR 不应靠更多后处理证明自己

#338 已修中性颜色通道泄漏，并有十平面 16/32 帧有界采集；不要重做。它没有消除有效通道的模型漂移。其 `ComposeCS` 通过同 recording 的 N-grid carrier 逐通道判定是否允许模型增量，而不是将任何一个通道的编辑资格传播给所有 RGB。

下一步仍按 #309：补齐同输入、同一 NR 结果的 GPU 空间恢复参考，以及原生曝光证据；复用现有 Flight/worker/精确退休/有界预算。比较普通恢复、同源空间参考、私有 SR，不能跨不同 N、不同 NR 输出、不同帧相位做“算法对比”。如果私有 SR 没有稳定净收益，就保留普通恢复为基线，实验默认关闭；不为挽救它继续叠空间滤波与时域累积。

画质采用约束式判定：目标区域确有可见改善，同时暗部、头发、细线、文字、Bloom、高亮色相、运动显露没有不可接受损失。NR 有意改变外观，PSNR/SSIM 相对原画下降不自动等于失败；锐度/局部对比上升也不自动等于变好。静态同源图、运动短片和同曝光显示变换共同判定。

来源：[Compose shader](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/ad37757cc5eed7b4e62bf63e445d28f1d4677837/experiments/nr-edit/shaders/nr_post_residual.hlsl)、[私有 SR Flight/计时](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/ad37757cc5eed7b4e62bf63e445d28f1d4677837/src/nr_private_sr_product.inl)、[#338](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/338)、[#309](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/309)。

### 6.3 多层 NR：比较整链，不单独神化“末层 100%”

保持已有残差恢复框架，优先比较单层、三层、五层的边际收益，以及同成本下“少层高分”和“多层低分”。后续实验可以比较统一低分、前面低分末层高分、原始高分细节保留+低分编辑恢复，但不立刻增加所有用户可见旋钮。

简单面积估算只用于排除误解：同一个输出基准下，三层 50% 的模型像素和是 3×0.5²=0.75；两层 50% 加末层 100% 是 1.5，翻倍而非几乎免费。真实成本还包含模型固定开销、尺寸对齐、拷贝与历史，必须实测。

合成定义须写清：最终输出减原始输入代表整链编辑；最终输出减第一层输出仅代表后续层增量，会丢掉第一层已做的改动，不能直接互换。只有颜色域、相位、分辨率、曝光一致后，才讨论首层外观与后续细节的独立权重。拒绝跨层共享历史、在模型累计变软后用锐化伪装为细节恢复。

### 6.4 时域、光流和 HDR 的顺序

建议先验证原生 MV/depth/jitter 的网格、单位、有效区域与历史采用，再在同一片段比较 NVOF/AMDOF。AI 新生成的外观不必严格服从几何 MV，光流也可能把外观变化当位移；没有遮挡/显露/切镜头置信度，不应给全屏持续累积。失败时退回本帧普通恢复，而不是携带不可信历史。

屏幕输出为 SDR 不代表内部场景输入不是 HDR。人脸/Bloom 变黑应先沿原图→NR 输入→NR 输出→合成→tone mapping 找到最早分歧，并检查 exposure/pre-exposure、归一化、负值/高亮与保护门。不要先调全局 gamma、钳掉高亮或强锁亮度掩盖错误。此节是研究顺序，不声称本轮已经证明具体游戏根因。

## 7. 外部参考：借鉴 Opti 的问题拆解，不照搬保证

本轮实际读取了 OptiScaler multipass 开放 PR #123 及其 fork 源码，固定 head `8fbdfa60df631c1f41bd39cf6afcbd96d76f1979`，不是假定功能已合入 upstream main。

源码路线是先做 R→R 的私有 RR/SR，让 NR 看较干净低分图，再把 NR/clean 的比值重采样到 raw 相位，交给原生 upscale。创建 HDR/auto-exposure、逐帧 pre-exposure、懒分配 step-specific 资源、切 step 保留已有一比一历史，都是值得研究的分解方式；其 epoch 准入不替代本项目的精确 recording/fence/queue 门。

作者报告的一游戏/一 GPU 成本为低分前置 3.0 ms、denoise-first 5.5 ms、高分后置 8.2 ms；这只说明它在该样本里位于两者之间，不是免费多跑一次 RR，也不是本插件的预期收益。所谓无外部 accumulator 只是不增加那一层历史；NR/RR 自己仍有历史，不能承诺无拖影。

#342 已允许并实际推进 raw guides 的明确近似复用。不要因缺少“精确 clean visibility producer”而禁止一切近似实验，也不要反过来把近似导引当已证明的几何合同。比值在近零/负值/FP16/相位边界的行为以及不编辑区域 bit-preservation 必须留在验收中。本轮不复制 GPL 源码/shader。

本轮没有完成 Magpie/RenoDX/Luma 当前源码的同等深度独立审查；它们保留为分工任务，不能把旧讨论或本仓库引用当它们最新实现已验证。

来源：[Opti PR123](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/pull/123)、[实际 DenoiseFirst 源码](https://github.com/GrimsVerk/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_DenoiseFirst.cpp)。

## 8. 操作逻辑：不要重做已完成的首页，修正真实耦合

已读到的 Beta15 UI 源码，顶部已经是版本/状态，下面才是 NR 开关；工作比例松手提交、编辑目标/配置代次冲突保护、失效实验说明和保持旧配置都有实现。`DrawNrActivityStatus` 也明确“近期提交不等于已显示”，持续旁路有 F8 提示。不能再用九月的旧界面印象要求重排同一件事。

**具体缺口一：恢复按钮跨越外观和成本。** `RestoreRecommendedPictureSettings` 调用处还显式切回 Before；tooltip 说明单层/100% 工作量等也会恢复。说明并非缺失，但用户只想恢复颜色时可能意外触发大规模配置改变。建议保留兼容的原动作，再明确拆分“恢复颜色与光影”和“恢复全部推荐设置”；后者展示将改变的路线/层数/工作尺寸，提交一次。不要悄悄改变旧 INI 或现有按钮含义。

**具体缺口二：画质对比与性能对比混在 F6。** 建议沿 #333 复用现有诊断能力提供“只显示原画，NR 照常运算”的有界对比；它必须保持模型输入/历史/层数/调度成本，不能新建第二份 NR。F6 是开关和成本对比，两者明示区分。没有经过实际路径验证前不向玩家宣传为等成本比较。

**具体缺口三：把请求与实际放在一起。** 保留顶部常驻，显示用户请求、实际路线、最近实际工作尺寸、实际层数、是否等旧模型退出。百分比边上明确基准；同一个 61% 可能不是同一 N/R。模型准备中不要显示旧配置“生效”。曝光/导引不支持时禁用相关操作但保留保存值，并说明仍然有效的功能。

建议页面逻辑保持渐进：画面放常用外观与工作量；光影/人物做局部细调；高级兼容放路线和诊断状态；实验项按“要解决什么问题”归类；反馈集中 F8/采集/版本身份。模型强度、最终混合、色彩范围、NR 分辨率分别解释，不能用一个模糊“强度”把四件事耦合。外观预设不应暗改工作量或 FG；性能预设先由测量确定，不在本报告里编造最佳百分比。

这是源码级交互审查，未实际打开游戏 Overlay，未完成视觉、字号、缩放或无障碍验收。

来源：[统一 UI](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/ad37757cc5eed7b4e62bf63e445d28f1d4677837/src/nr_unified_settings.inl)、[状态解释](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/024f9cc32778c1056db364d76d087ee257da6502/src/nr_settings_ui.inl#L530-L677)、[#196](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/196)、[#209](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/209)、[#333](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/333)。

## 9. 可并行的本地工作包：12 个审查位 + 4 个交叉审查位

**以下为未启动的分工建议，不是本轮运行记录。** 由现有 coordinator 在执行环境可用、预算允许时分配；先只读。不是要求所有人同时改 `nr_before_sr.cpp`，也不是另建 16 个长期分支。

| 位 | 唯一问题与输出 | 既有入口/所有权 |
|---|---|---|
| 01 生命周期 | #341 的死亡/重放/late-attach 覆盖与组合边界 | #339/#341；不改冻结 PR |
| 02 CPU 热路径 | 实测 precheck 命中、snapshot 成本、锁等待 | #277/#333；先给热点证据 |
| 03 冷启动 | Load/Create/首 Eval/首有效输出分账 | #293；不后台借用 game list |
| 04 显存与缓存 | 当前复用后还剩哪些 owner/高水位/压力路径 | #329/#330/#331/#343 |
| 05 私有 SR | 同源普通恢复/空间参考/私有 SR 的净收益门 | #309/#338；不新增滤镜救场 |
| 06 两 RR | 已授权两组长序列与导引近似边界 | #309/#342；只准备/复核，不抢 GPU |
| 07 多层 NR | 同成本单/三/五层与高分末层比较设计 | #9/#309；不共享历史 |
| 08 MV/光流 | 坐标、相位、遮挡与切镜头的最早失效证据 | #274/#296；无效先退当前帧 |
| 09 HDR/颜色 | 暗脸、Bloom、99/100 跳变的阶段归因 | #309；不换全局颜色算法 |
| 10 操作逻辑 | 恢复动作、请求/实际、等成本对比与控制可用性 | #196/#209/#333；保留设置 |
| 11 兼容性 | 原生 SR/RR/FG owner，D3D11/无 DLSS provider 合同 | #164/#167/#231；不引入第二 presenter |
| 12 外部源码 | Magpie/RenoDX/Luma/Opti 的固定 SHA、实际实现与许可 | #274/#309；不凭 README 宣称采用 |
| X1 正确性反审 | 质疑前 6 份的对象寿命/单位/对照身份 | 只读，无独立产品修改权 |
| X2 画质反审 | 质疑后 6 份的细节损失/曝光/时域误判 | 只读，不“多数投票”代替证据 |
| X3 测试方法 | 核验负控、样本独立性、采集排除、unknown/0 | #333，复用现有框架 |
| X4 集成与发布 | 检查依赖、容量、源/包身份、回退与文档 | #311；最终由 Claude 整合 |

每份只交：固定源码位置、观察/假设区分、最小修改建议、反例、成本指标、验证状态、与哪个正在进行的 PR 重叠。没有新证据就写“未找到可支持的改动”，不为凑工作量造优化。

允许源码阅读、CPU 小测试、审查并行；Windows canonical、真实 NGX、GPU 测量和部署由单一执行者排队。不同 GPU 只有在各自硬件/二进制身份独立且资源隔离时才可另开测量；不混用结果。对同一个热文件最多一个写入者，按 coherent slice 合 PR，保持最多一层显式依赖。

## 10. 后续实施顺序与停止条件

| 顺序 | 最小交付 | 停止/回退条件 |
|---|---|---|
| 第一批 | 收敛 #341 与 #343 组合门；#342 完成已授权两臂；保留现用候选 | 身份不匹配、真实默认入口不支持、门禁失败即保留失败；不绕过 |
| 第二批 | 热点证据充分才实施 CPU-A/B；补首开和闲置驻留的实测 | 功能不等价、旁路增多、帧时间无净收益即不合并 |
| 第三批 | 私有 SR/两 RR 做一次明确质量-成本判决；多层建立少量非劣配置 | 只有更软/更锐、缺少目标改善或成本不值则保留普通恢复 |
| 第四批 | 小幅解耦恢复按钮、对比模式和状态；从实测配置选性能/均衡/画质预设 | 不允许设置迁移破坏、额外模型、自动换 FG、用户未知的成本变化 |
| 后续研究 | 原生 MV 后再评估光流、ROI/异分辨率层、通用 provider | 不抢当前生命周期/默认链路验收的优先级 |

不存在一组对所有游戏/显卡/审美同时最优的参数。交付目标是形成有数据支持的非劣解集合：同画质更快、同成本更好，或清楚展示额外成本换来的效果。不能通过更低实际采用率、失效实验、模糊掉细节或偷偷降档制造“优化”。

## 11. 本轮验证记录

- GitHub：实时读取与固定 SHA 的专项源码审查；未声称整库逐行审完。
- 独立 Python CPU 决策模型：运行通过 9,576 组；两个错误候选被识别。模型未直接编译生产函数，不证明 D3D12/COM/并发。
- Windows 编译、canonical、WARP、NGX、游戏运行、Overlay 视觉验收：本轮均未执行。
- 现有 PR 的测试结果仅按各自 source/binary 身份引用，未重跑，未转记到本 PR。
- 本 PR 只增加本报告和独立 CPU 模型；不调整生产文件、配置、版本、CI、运行库或 main。
- 没有创建额外总 Issue、关闭 Issue/PR、合并/发布、打包/部署，或安排后台任务。
