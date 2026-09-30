# #298 Beta 14 F8 分配分类账 / F8 allocation ledger

## 中文

这项改动给 F8 的 `start` 和 `end` 元数据增加插件自建 D3D12 资源分类账。它用于先观察分配归属和生命周期，再判断是否值得优化；不改变渲染、资源创建或释放、配额、槽数和已保存设置，也不承诺显存下降。

每个资源通过自身的 D3D12 device 和创建节点查询 `GetResourceAllocationInfo.SizeInBytes`。结果是该资源的**分配尺寸**，不是已驻留字节、显卡物理占用、DXGI 整个进程的增量或预算。查询失败、零或无效尺寸记为未知。借用的游戏输入、输出、曝光和导引资源不计入；同一资源指针的多个引用只计一次。只有两条 F8 worker 在开始和结束时采样；发起 F8 的 UI/事件及普通帧不执行资源查询。`sampleStartedTickMs` 与 `sampleFinishedTickMs` 是每次实际采样区间的单调时钟刻度。当前枚举的 Core、兼容 Present 和 FG 所有者在 Core 所有者锁内读取；采样释放 Core 锁后，才持反馈锁追加报告，与仍在写入的观察线程串行。其他模块的计数在另一时刻读取，F8 不等待 GPU fence。

`allocation.<类别>.<生命周期>` 给出所有堆类型的合计字节、资源数、未知组数及溢出标记。`heap.default`、`heap.readback`、`heap.upload`、`heap.other` 是同一合计的细分，**不能再与合计相加**。只输出非空的堆行；缺失的堆行四项均为零，类别/生命周期合计行始终输出。`knownBytesLowerBound` 只将每个类别/生命周期的已知字节加一次。`complete=0` 表示仍有未枚举的所有者，不能将下界视为插件总占用；`overflow=1` 可能是数值/计数饱和，或有界去重表耗尽，此时不能把下界当完整计量。

| 范围 | 当前记录 |
|---|---|
| Core 主槽及辅助槽 | 已证明由槽持有的源/结果、中间图像、运动代理、实验平面、阶段捕获和部分回读缓冲；分 live、retired、pending。辅助槽对双层空间平面的引用不重复计数。 |
| 双层空间、兼容 Present、FG | Core 双层空间与兼容 Present 每个额外层的 `SpatialSlot` 均计入自有 output、条件自有 input、delta；`MatchedResidual` 首个缩小槽另计自有 low-valid。cumulative 后续槽只借用首槽 low-valid，不重复计；primary 同样借用。FG 可见的自有资源按所属类别记录。其他未枚举模块仍使整体结果不完整。 |
| 私有 SR、时域、绑定池、截图/回放等异步所有者 | 可确认的所有者或活动计数标为未知；未提供安全资源级枚举的模块使全局 `complete=0`。销毁已移出所有者表时按 `destroyingContexts` 标未知。其计数归零不能单独证明释放成功：生命周期跟踪失去确定性后，`detachedQuarantinedUnknown=1` 保守表示可能仍保留在表外的 Core 资源和模型，不能推算准确数量。 |
| 兼容 Present 锁外退休 | `RetireCurrent` 临时清空 `current` 并在链锁外释放时，`compatRetiringUnknown=1`，compat/destroying 为未知；不能把空指针误读为零资源。 |
| NR 模型、PSO、描述符堆 | NR 只报告每层 live/retired/pending 实例数；厂商模型内部、PSO 和描述符堆的真实 GPU 字节均为 `unknown`。`GetCachedBlob` 的 CPU 序列化数据不得当成 GPU 字节。 |

原有 `processMemory` 的 DXGI 进程 `CurrentUsage/Budget`、空间实验/私有 SR/时域/双层的已有字节或预算字段仍独立展示。这些口径与分类账不同，不参与 `knownBytesLowerBound`，也不能与之相减来推算厂商模型显存。

后续固定场景测试应保持同一游戏、分辨率、画质、机位和工作量，在 NR 单层与多层的稳定段分别保存 F8 `start/end`；记录配置、已完成的 fence/退休状态和 DXGI 进程用量。重点对照层数变化、菜单往返、三层回到一层后资源是否在自身 fence 完成后退役，再分析短峰或长期增长。历史进程差额只是线索，不等于插件分配分类账，也不能直接据此降档或更改预算。

## English

This change adds a ledger for Core-created D3D12 resources to the F8 `start` and `end` metadata. It records ownership and lifetime before considering optimization. It does not change rendering, allocation or release behavior, budgets, slot counts, or saved settings, and makes no memory-reduction promise.

Each resource is queried through its own D3D12 device and creation node with `GetResourceAllocationInfo.SizeInBytes`. This is an **allocation size**, not resident bytes, physical VRAM use, a DXGI process-usage delta, or a budget. Failed, zero, and invalid results are unknown. Borrowed game input, output, exposure, and guide resources are excluded; repeated references to the same resource pointer are counted once. Only the two F8 workers sample at start and end; the initiating UI/event and normal frames do not query resources. `sampleStartedTickMs` and `sampleFinishedTickMs` identify each actual sampling interval on a monotonic clock. Core, compat Present, and FG owners are sampled under the Core owner lock. After sampling releases that lock, report text is appended under the feedback lock, serialized with any still-running observer. Other module counters are sampled at separate moments, and F8 does not wait for GPU fences.

`allocation.<category>.<phase>` reports bytes, resource count, unknown groups, and overflow across all heap types. `heap.default`, `heap.readback`, `heap.upload`, and `heap.other` break down that same subtotal and **must not be added to it again**. Only nonempty heap rows are emitted; an omitted heap row has four zero fields, while every category/phase subtotal is emitted. `knownBytesLowerBound` sums known category/phase bytes once. `complete=0` means owners remain unenumerated, so this lower bound is not total plugin memory. `overflow=1` can mean saturated bytes/counts or exhaustion of the bounded alias table; in that case the lower bound cannot be treated as a complete measurement.

| Scope | Current record |
|---|---|
| Core primary and auxiliary slots | Provably owned source/result and intermediate textures, motion proxy, experimental planes, stage capture, and selected readbacks, split into live, retired, and pending. Auxiliary references to dual spatial planes are not counted twice. |
| Dual spatial, compat Present, FG | Core dual spatial and every additional compat Present `SpatialSlot` count their owned output, conditional owned input, and delta. The first reduced `MatchedResidual` slot also counts its owned low-valid texture. Cumulative successors borrow that low-valid texture from the first slot and do not count it again; primary is borrowed too. Visible FG-owned resources are measured in their category. Other unenumerated modules still make the overall result partial. |
| Private SR, temporal, binding pools, screenshot/replay and other asynchronous owners | Confirmed owners or activity are marked unknown where safe resource enumeration is unavailable. Unenumerated modules keep global `complete=0`. Resources detached for destruction are represented by an unknown `destroyingContexts` count. A zero count alone does not prove release: after lifecycle tracking becomes uncertain, `detachedQuarantinedUnknown=1` conservatively records possible Core resources and models retained outside the tables, without inventing a count. |
| Compat Present outside-lock retirement | While `RetireCurrent` temporarily clears `current` and releases outside the chain lock, `compatRetiringUnknown=1` and compat/destroying remain unknown rather than appearing to be released. |
| NR models, PSOs, descriptor heaps | NR reports per-layer live/retired/pending instance counts only. Actual GPU bytes inside vendor models, PSOs, and descriptor heaps are `unknown`. CPU serialized `GetCachedBlob` data must not be presented as GPU bytes. |

Existing DXGI process `CurrentUsage/Budget` in `processMemory`, and existing spatial/private SR/temporal/dual byte or budget fields, remain separate reported metrics. They have different meanings, are excluded from `knownBytesLowerBound`, and cannot be subtracted from it to infer vendor-model memory.

A later fixed-scene test should hold game, resolution, quality settings, camera, and workload constant, then capture F8 `start/end` during stable one-layer and multilayer NR runs. Record configuration, completed fence/retirement state, and DXGI process usage. Compare layer changes, menu round trips, and whether resources retire after their own fences when moving from three layers back to one before investigating short peaks or long-term growth. Historical process deltas are clues, not this allocation ledger, and do not by themselves justify quality downgrades or budget changes.
