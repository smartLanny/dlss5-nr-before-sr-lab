# unified8 自有临时纹理寿命审查

日期：2026-09-22。对象：从 `8a9f7378` 开始的 unified8 工作源码。
这是源码寿命审查，不是最终交付身份、GPU 显存测量或游戏验收。

## 结论

本轮没有找到能在现有模型、录制和同步合同下证明安全的新增纹理复用。
不修改资源分配、快照、层数、比例、历史或同步；新增显存节省记为 **0**。
已实现的辅助着色器预编译属于创建成本优化，不能算作资源复用或游戏帧率收益。

一至五层同尺寸链已经把前层输出直接作为后层输入。后层只新增独立的
FP16 模型输出，没有再复制一个同尺寸输入。多个 COM 引用不等于多个纹理分配。
“这个 dispatch 已结束”也不足以证明可覆盖资源：同一录制中的后续消费者、
可重放录制和不透明 NGX Feature 的保留引用仍须计入。

## 已确认的寿命表

`R` 为原始/最终图尺寸，`N` 为整个 NR 链统一工作尺寸；不改变任一尺寸。
行号对应本次检查时源码，后续生成补丁可能移动行号，以函数名为稳定锚。

| 对象 | 分配与已有复用 | 写入与最后可见消费者 | 本轮决定 |
|---|---|---|---|
| Present `raw` | 原呈现格式、R；每个 Context 一张，三 flight 共用 | `Frame` 复制 backbuffer；最终 `BlitPS` 仍读 raw，以保留原始编码、alpha 和无修改路径 | 保留；不能在 Convert 后覆盖 |
| Present `image.source` / `image.result` | 各一张 FP16、R | source 被 Encode/Combine 读取；最终 `BlitPS` 同时读取 source、result、raw | 三者不能合并，即使尺寸或格式偶合 |
| Present `image.proxy` | FP16、N，一张 | Encode 写；首层模型读；最终合成仍读原 proxy 计算模型残差 | 不能当后层输出缓冲 |
| Present P1 及后层输出 | 各层一张 FP16、N；后层输入 AddRef 前层输出 | 独立 Create/Evaluate 参数；下一层输入；末层用于合成；原 P1 仍由 Feature 及引用链保留 | 不做层间 ping-pong，不改变模型资源身份 |
| Present 未知导引占位 | depth R32_FLOAT、motion R16G16_FLOAT、N；全链只分配一对 | Bootstrap 初始化一次，之后同尺寸全链只读；没有原生导引证据含义 | 已共享，不重复计节省 |
| dual DirectSecond | 每个后层槽只新增 FP16 output；input 与 primary 为同一已固定资源 | `RecordSecond` 输出供后层/最终消费者使用；外部 Create 可以保留资源 | 已省去 low_input / validity / delta，不再合并输出 |
| dual reduced/cumulative 兼容路径 | 首次 reduced 有 low_input、validity、delta；cumulative AddRef 首次 low_input/validity | delta shader 同时读输入、输出、有效性；最后合成消费 delta | 已有共享；生产同尺寸入口不把该兼容路径计为新收益 |
| Core 原生后置 `source` | 自有 FP16 SR 输出快照；游戏输出另有独立所有权 | 原生 SR 完成后写快照，编码及最终合成读快照；游戏目标随后被写 | 保留快照；不借游戏 SR 输出代替 |
| Core 前置 `result` | 自有 FP16、R | Combine 写，原生 SR 随后读取 | 不是合成结束即可释放的 scratch |
| 原生导引 `guide_copies[8]` | 每份拥有 depth/MV；相同 device/尺寸/格式及 snapshot 类型会缓存复用 | 原始录制生产；Present 可在另一队列消费；生产和消费各有真实 fence | 现有双 fence + reserved/acquired 门决定复用，不压成单份 |
| RR hardware depth | 每 NrSlot 缓存 R32_FLOAT 导引平面；尺寸不变时复用 | 转换写，绑定给对应 Feature；描述符可能已封存 | 不跨独立 Feature/槽共享；需先证明引用和录制寿命 |
| motion_proxy | 每槽缓存 R16G16_FLOAT、N | 导引转换/快照写，模型或已接时域消费者读取 | 不与模型/OF 历史或其它槽合并 |
| `edit_field` | luma 为 R32G32_FLOAT；color 为 R32G32B32A32_FLOAT；N | Build 写，Restore 和最终 Combine 读；Restore 无效分支仍会回读原 field | 保留；与 FP16 proxy/result 格式不同 |
| `edit_horizontal` | R16G16_FLOAT、Rw × Nh | 横向恢复写，最终 Combine 读 | 与 field 格式不同；与一般 motion_proxy 尺寸/消费者不同 |
| `lanczos_intermediate` | FP16、Nw × Rh；仅前置且缩放非 1:1 | 水平滤波写，垂直滤波读；随后模型使用独立 proxy | 一般尺寸与 N、R 均不同；不为罕见单轴尺寸偶合改资源合同 |
| `region_guide` / `region_field` | 同尺寸 R32G32B32A32_FLOAT 两张 | BuildCS 一边读 guide 邻域、一边写 field；Combine 随后读 field | 同一 dispatch 读写重叠，不可原地合并 |
| face tracker guide/mask/payload | 有自己 ledger、录制身份和历史引用 | mask 给当前消费者；guide 能被后续帧历史读取；payload 后有 readback | 保持独立；不把历史当 scratch |
| timestamp/readback | 每 Present flight / Core slot 分开 | GPU resolve 后 CPU 读取，受对应 fence、generation 和 recording 状态约束 | 本轮不改 buffer；合并需不重叠 offset 与消费证明 |

### 关键源码锚

- `nr_compat_present_models.inl:173` 的 `Build`：五张基础颜色纹理；
  `:187` 单份占位导引；`:214` 多层资源链；`:240` 已不为后层分配无用 Bootstrap heaps。
- `nr_compat_present_events.inl:92` 的 `Frame`：同一真实队列串行复用 scratch；
  `:102` 原图复制；`:114` 层循环；`:163` 最终合成；`:166` 最终绘制。
- `nr_compat_present_shader.h:45` 的 `BlitPS`：raw、Canonical、Composed 同时读取。
- `nr_compat_present_models.inl:103` 的 `RetireProof` 和 `:108` 的 `Release`：
  exact timeline drain 后，先释放所有模型及参数，成功后才释放依赖纹理。
- `nr_dual_spatial_d3d12.cpp:218`–`:264` 的 `CreateSpatialSlot`：实际分配公式、
  AddRef 和 `!residual` 早退；`:382` 的 `DestroySpatialSlotAfterRetirement` 明确
  外部 Create 也可能录制依赖，不能只看本模块 `recorded` 位。
- `nr_dual_runtime.inl:119`：一个主槽映射一个子槽，原资源变更退休整个子层；
  `:145`–`:148` 的 source/proxy/neural 是 AddRef，不是额外纹理分配。
- `nr_codec_descriptors.inl:19`–`:26`：source、proxy、最终输出/残差分别绑定；
  `nr_before_sr.cpp:4110` 的合成读取示例同时需要 original、proxy 和 neural。
- `nr_source_transport.inl:38` 的 `GuideReusable`、`:212` 起的缓存尺寸/格式检查、
  `:403` 的 `ReleasePresentGuides`：保留两端 fence 和 acquired/reserved 状态。
- `nr_rr_depth_adapter.inl:149` 的 `EnsureRrDepthSlot`：缓存平面及安全替换；
  `:208` 的 `DispatchRrDepth`：实际转换与状态转换。
- `nr_before_sr.cpp:5918` 的 `CanMutateSlotLocked`：录制引用为零、没有待 Signal、
  slot fence 完成且 lifecycle 确定；`:8026` 的 `CreateSlot` 分开 native snapshot、
  proxy、neural 和 pre-result；`:8290`、`:8369`、`:8411` 为三类 optional scratch。
- `nr_region_runtime.inl:66` 分配两张平面；`nr_region_shader.h:110`–`:119`
  的 BuildCS 读取 guide 邻域并写 field，不能做无屏障原地覆盖。
- `nr_face_tracker_gpu.inl:136` 起绑定历史 guide；`:183` 的 Collect 要求 ledger
  已提交、录制不再存活和 fence 完成；本轮不触碰此历史及回读流程。

## 同尺寸层数成本：能确认与不能确认的部分

对于 Present Context，设层数为 n（1–5）：自有颜色纹理总数是 `n + 4`，
其中 FP16 为 `n + 3` 张，另一张 raw 保持呈现格式。另有按需的一对未知导引。
这不是把每层重复分配完整的 source/proxy/result 套装。Core inline 后层的
DirectSecond 分支同样每槽每层只增一张 FP16 输出；另计实际需要的导引适配。

字节数必须用设备 `GetResourceAllocationInfo`，不能用纹理张数、像素字节数
或 COM 引用数冒充实测显存。现有 owned 账不含不透明 NGX 内部分配，也不能
把 guide ring、其它槽的 motion_proxy、capture 或模型/OF/SR 历史漏掉后称为总显存。

## 尚未证明可实施的候选

1. 同尺寸后层 RR depth / MV 转换可能处理同一份导引。但跨 Feature 共享需要
   对源帧、guide contract、不可变描述符、Create 引用及全部录制/队列消费者建立
   明确所有权；当前没有这种证明。不以“相同尺寸”代替它。
2. 中间层颜色输出看似在下一层结束后不再被本模块读取，但 NGX 引用和独立模型
   合同不透明；ping-pong 还会复写先前模型绑定的资源。本轮不实施。
3. 小型 readback buffer 可探索一个堆中的不同 offset，属于独立设计；不能覆盖
   尚未被 CPU 收集的区间，也不能删掉 fence 或 recording lease。

## 创建成本边界

本包辅助预编译精确覆盖 Present 5、dual 2、原生导引 snapshot 1、RR depth 1，
合计 **9 个入口**；既有核心 26 CSO 不重复计入。完整 Core 定向宿主证明这九个
入口实际 D3DCompile 从 9 次到 0 次、8 次 PSO 创建保留、九份编译字节完全相同。
该对照创建真实 WARP PSO，但不执行 dispatch、队列提交或 NGX。
这不是“全 Core 不再运行着色器编译”：例如 `nr_region_runtime.inl:25` 的
SnapshotCS/BuildCS、`nr_face_tracker_gpu.inl:70` 的三个 face 入口仍在各自路径
运行时编译，其它变体应单独列账。

新增创建边界计时可区分辅助 D3DCompile 和 PSO 创建；模型创建须看独立 NGX
Create 计时，不把 PSO/编译耗时归入模型，也不把单次宿主冷启动值推算为游戏 FPS。

## 验证范围

本轮只读取源码并追踪分配、引用、GPU 读写和退休调用，没有运行 GPU、修改生产
资源代码或增加镜像实现的测试。已有辅助预编译验证不算作本表资源复用的验证。
若未来实施复用，须先构造真实分配/descriptor 身份 oracle 和多录制、多 flight、
热切换、失败隔离的拒绝案例，再按实际队列/fence 验证像素一致性及 owned 字节差。
