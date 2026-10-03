# Beta 15 / #309：实际 Reset 记录与同源空间参考

This is development evidence, not a game-ready quality fix or release candidate.

## 目的和范围

维护者报告私有 SR 有边缘抖动、碎边和额外开销。先区分实际 Feature1
历史是否连续，以及缩小/编码/恢复带来的误差，才能判断该修什么。
基线为 main `024f9cc3`（Beta14）；Beta14 交付和基线文档独立在 #337。

本批不改变默认渲染路线，不删除 Reset 或生命周期保护，不加入 jitter
猜测、光流、私有 Feature13 或新时域算法；新产品身份先升至 Beta15。

## 产品内记录 / Product diagnostics

- `DLSS5_PRIVATE_SR_EVIDENCE_FRAMES` 仅精确值 `16` 或 `32` 启用，
  默认关闭。当前记录的是完成调用的元数据，**没有游戏图像读回**。
- 保存实际 Bind Reset 原因、历史原因、前驱与 motion reference、原生
  Reset、源间隔、导引网格、原生 render-pixel jitter 和归一化 MV 单位。
- 日志必须等同一 recording 已提交且释放、同队列对应 fence 确实完成。
  指针存在只写 `planeBindingsPresent`，不当作采集成功。
- 原有 `frame.reset || history_reset || !matched` 条件保持等价。
  创建、忙、失败和中毒本身不逐帧输出；完成帧选择也不保证游戏连续帧。
- 关闭记录时无新增 GPU 指令、读回或等待；仅增加内部 CPU 元数据。
  未修改公开 DLL ABI，完整链接/导出仍由 canonical 构建确认。

## 受控参考 / Controlled reference

开发宿主保存同帧 B、Z、carrier、tail、私有恢复、最终结果和导引。
E_N=`D(Z-B)` 与 support 是从原始读回离线派生，分别标记。

空间参考从同一 carrier、tail、support、phase 和 Encoded 身份出发，
调用相同生产 Compose，目标是本帧 Z 的独立副本。
`RecordSpatialReference` 只接受首次 Compose 已成功、尚未提交的同一
recording；创建阶段、退休、错误 recording 和目标别名必须拒绝。
产品流程不调用此开发接口。

候选 `Z+(private_final-spatial_final)` 只在离线计算，用来隔离私有恢复
相对匹配空间恢复的增量。它尚未作为产品算法启用。

| WARP 静态 fixture，全 16 帧 RGB 最大差 | 结果 |
|---|---:|
| private final 与同 GPU 空间参考 | 0 |
| 离线候选与 Z | 0 |
| 零修改区域候选与 Z | 0 |
| CPU carrier 插值与 GPU 重建 | 0.00048828125 |
| CPU 解码与 GPU Compose | 0.00048828125 |

源序号 10–25 连续，逐帧确认 recording 释放和 fence 完成；实际 Bind Reset
仅首帧。fixture 是 GPU 空间 pass，**不是 NGX Feature1 推理**。
恒等断言要求前两项精确零；CPU/GPU 复算容差单列，不能冒充模型增益。
旧 CPU 参考的 0.00146484375 最大差不当作 Feature1 画质结论。

验证入口：`experiments/nr-edit/tools/validate_private_sr_quality.py`。
私有模块五对象/八个 shader、Core 翻译单元语法、产品版本检查和差异检查
已完成；完整双语 canonical 构建另由 PR 的 `full-build` 门运行。

## 后续门槛 / Remaining gates

1. 在 Finish 的原 Flight 上、Record 后且提交前，增加有界游戏平面采集：
   16 帧（必要才32），B/Z/最终与归一化 depth/MV、carrier/tail/private。
   分配或容量不足只跳过采集；读回等原 recording/fence 完成，
   不改变渲染准入、提交或正常关闭性能。
2. 真实 Feature1 宿主与游戏分别检查连续历史、亚像素移动、停镜、99%及
   实际 N 比例、HDR、文字/细线和区域外内容。本批静态零 MV 不能证明它们。
3. 只在证据支持时实现最小画质修复，再独立评估总成本和玩家候选。
   私有 SR 关闭与普通恢复继续保留；NR→一次原生 RR 仍是独立未实现路线。

硬件 NGX 与游戏连续像素证据尚未执行；本批不生成玩家候选、不部署游戏。
