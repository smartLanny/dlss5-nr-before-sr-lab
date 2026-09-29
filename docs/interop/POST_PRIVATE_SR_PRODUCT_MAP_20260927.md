# 私有残差 SR：产品接线核查与下一开发批次

> 历史静态核查：下文保留 main/Beta 3 接入前的缺口与证据。当前 #313 已继续实现，最新接线与验证以 [Beta 4 功能矩阵](052_BETA4_UNIFIED_ACCEPTANCE.md) 为入口；下文“未接入”仅针对所列核查基线。

核查基线：Core `b2990b120337cdd8026f83a1bd369f3708b78fbe`；#313 文档/回执
`6fb01d7338d1b06b473d1958bd6b05f46060f084`，实现 `c0eb2af2`。本次是
源码接线核查，未新增运行时、未重跑模型、未把 #313 接入产品。实验库合同以
[#313 的 POST_PRIVATE_SR.md](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/6fb01d7338d1b06b473d1958bd6b05f46060f084/experiments/nr-edit/POST_PRIVATE_SR.md) 为准。

## 已找到的画面生产者

| 需要的对象 | 当前生产者 | 可直接复用与限制 |
|---|---|---|
| 原生基线 B | `src/nr_before_sr.cpp` 的 `FinishAfterSr` 调用 `DispatchPostSrNativeSnapshot`，从原生 Output 的活动矩形复制到独立 `slot.source` | 原生 Evaluate 已成功；B 在 NR encode 前形成。FP16 路线由 `src/nr_post_sr_snapshot.inl::CopyPostSrFp16Snapshot` 位值复制至零原点。 |
| 最终候选 Z | `FinishAfterSr` 中全部 `nr_dual_runtime::Run`、修改场/区域策略及最终 `g_codec.combine` 之后的 `slot.result` | 不能取 `slot.neural`，也不能只减最后一层。后续生成补丁仍会改变最终消费者。 |
| 时域之后的 Z | `scripts/patch-temporal-runtime.py` 在 combine 后、`RecordVisibleTransferLocked` 前插入 `nr_temporal::Record` | 若保留时域组合，候选位置应在该调用成功/普通拒绝后；Faulted 沿现有隔离返回，不能转成普通 fallback。 |
| 最终恢复策略 | `scripts/patch-reconstruction.py` 更新 combine 的低网格配对与近黑保护 | 接入和测试必须使用完整生成链，不能只看 tracked 基础 CPP 或旧 Deferred 实验。 |

第一批沿 #313 已验证合同限定为 **原生 Feature1 后置、FP16 SDR、单节点、已证明的
direct queue**。B 的矩形为 `(0,0,O)`；Z/target 使用原生 Output 的活动矩形，且
target 已含 Z。这样私有 SR 首次创建、容量不足或普通拒绝都保留已有结果。
FP16 格式不证明 SDR；原生 HDR 标记、颜色解释和当前曝光合同不能被伪装成 SDR。
R11/R8/R10 输出、RR、Present、HDR、FG 和多 GPU 不因 B/Z 指针可取就自动支持。

## 导引是实际缺口

当前 `patch-temporal-runtime.py` 的提前 MV 捕获只在原生 MV 防闪请求时触发。
私有 SR 需要独立的需求驱动，不能要求用户打开另一种时域算法才能获得导引。
`patch-post-compat.py` 中较晚发生的 MV 重采样，也不能证明原生调用后的游戏纹理
仍保留调用前的内容与状态。

下一批需要实现并验证：

1. 在原生读取边界取得同源导引快照，保留原资源有效区域、原始运动量单位与来源身份。
2. 提供 #313 要求的零原点、typed、N 网格 depth/MV；N 网格尺度不能照搬
   `nr_temporal_runtime.inl::PrepareForRoute` 给 O 网格消费者使用的尺度。
3. 明确 post-SR 相位、零 jitter 及 motion reference 的证明来源。纹理大小相等、
   可回读的 NGX 参数或手工填写 `known=true` 都不能替代这一合同；RR 占位深度也不算真实导引。
4. 对域、有效区域、来源、导引或相位缺失给出明确拒绝原因，保留 Z；不以开关显示开启冒充实际采用。

本次静态核查尚未找到完整满足上述合同的 N-depth 生产链；实现它之前，不能把私有
Feature1 接入状态标为“只差游戏测试”。

## 真实提交与退休接线

| 生命周期事件 | 现有 Core 位置 | 私有 SR 所需行为 |
|---|---|---|
| 录制身份 | `nr_source_frame_runtime.inl::RecordingOf`、`ReadLifecycleToken` | 固定 canonical list、lifetime/token、Reset 代次、device/queue 映射。裸指针不是完整身份。 |
| 录制依赖 | `CaptureSubmissionBatchLocked` 的 context/slot 租约 | 只要 `Result.recorded` 为真就登记，包括 WaitingForSr 以及只有编码/barrier 的调用，不能只登记 Composed。 |
| 提交成功 | `ProcessSubmittedCommandLists` → capture/pin → 原生 Execute → 同队列私有 Signal → `CompleteSubmissionBatchLocked` | 在真实成功门交付一次 `Submitted`，传准确 queue/fence/value；CPU 录制次序和 post-boundary 序号不算提交证明。 |
| Reset/Destroy | `ProcessCommandListReset`、`OnDestroyCommandList`、`DetachLifecycleCommandListTokenLocked` | 真实 Reset 成功或准确销毁后才发布 `recording_released`；失败不能释放引用。 |
| 提交不确定/未提交丢弃 | capture/Signal 失败、重放、队列错配、未提交录制结束 | 调用对应失败/Discarded 路径并隔离依赖；不能仅 Cancel 后清槽。 |
| GPU 完成和释放 | 既有精确 fence 完成及退休收集 | GPU 完成与录制已结束两者都满足后才能 Collect/Retire；API 所有者和 loader 模块寿命覆盖 context。 |

现有 temporal 提交回调由 `scripts/patch-temporal-runtime.py` 注入。可复用它的
成功门位置，不能照搬语义：temporal 可证明的 `cancelled_unsubmitted` 与 #313 的
`Discarded` 不同；后者对持有该录制的槽直接 Poison，`Cancel` 只请求退休/重置历史。
前序未成功提交时不能推进同一私有历史；已提交而尚在 GPU 执行的前序不应被额外 CPU 等待阻塞。

## 软件验证出口与后续画质取证

开发批次按“独立导引生产者 → 生命周期适配 → 完整生成 Core 接入”推进，不重写已验证的
载体/尾差数学。各阶段只在相应接口有实际实现后新增测试：

- 同帧 B/Z、全部层贡献、非零 subrect、奇数尺寸及 N/O 比例；零修改和小正负尾差保留。
- 非有限/越界 MV、错源/错相位、HDR/预览/占位深度拒绝；开关关闭时不分配私有历史或产生额外调度。
- 首次创建不合成；前序未提交拒绝、已提交在途允许；准确 Signal、重放、Reset 失败、未提交丢弃和队列错配。
- fence 完成但录制仍可重放时不可复用；录制结束但 GPU 未完成也不可复用；异常隔离不提前释放。
- 完整 `build-unified-candidate.ps1` 的最终生成与双语门禁；新的 Core 候选单独记录身份。
  旧 #313 人工静态 Feature1 回执和旧 UI 候选构建不替代这些新增软件检查。

新增噪点研究复用同源取证合同：保留 B、各层原始输出、最终 Z 和连续源帧，先区分随机
波动、稳定错误结构及恢复放大。目标仍是保护原图/文字/清晰边缘与有效 NR 修改，新增 GPU
时间目标不超过 3%，允许少量残留。不得预先宣称 AMDOF 或私有 SR 能消除所有黑点；先比较
空间恢复、仅 AMDOF、仅私有 SR，组合另验。独立 residual RR 继续单列。

真实游戏测试按维护者最新决定统一稍后进行；它不会补齐尚未实现的导引与生命周期代码。
