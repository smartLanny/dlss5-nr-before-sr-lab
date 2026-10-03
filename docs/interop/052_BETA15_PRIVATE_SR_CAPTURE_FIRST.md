# Beta 15 / #309：有界采集与中性通道串扰修正

## 2026-10-03 续接：候选范围

本批实现真实平面的产品采集接线，并修正受控实际 Feature1 已复现的中性通道
串扰。**没有采到真实游戏序列，不能称已修游戏闪烁或证明私有 SR 净收益。**
普通恢复继续作为基线；现有私有 SR 开关默认关闭，外部时域互斥、用户配置不变。

- 默认关闭的采集复用原 Flight、recording、队列和 fence。只有精确录制已提交、
  已释放且同队列 fence 完成，才交后台线程 Map/写盘；渲染不等待 GPU 或磁盘。
- `DLSS5_PRIVATE_SR_EVIDENCE_FRAMES` 必须精确为 16/32，像素另需
  `DLSS5_PRIVATE_SR_EVIDENCE_DIR` 指定现有绝对目录。无目录只保留已有元数据。
  捕获原始 depth/MV、B/Z、carrier/tail、private/final、归一化 depth/MV 十平面。
  完整合法输入不裁成 ROI；不明确的多平面/数组/mip 仅拒绝采集。
- 最多 64 attempts / 8 sessions；每帧读回分配最多 512 MiB，进程读回最多
  8 GiB 且保留 4 GiB 可用物理内存；总像素文件最多 8 GiB、磁盘保留 1 GiB。
  断源或合同变化另起 session，逐帧成功文件才可证明 16/32 帧连续完整。
- 采集及后台积压窗口不进入 private/acceptance/dual GPU 计时样本。
  关闭时不创建采集线程/读回资源、不增加采集 GPU 指令；不改 INI。
  产品采集没有匹配 GPU 空间参考，也没有原生曝光纹理读回，manifest 如实标缺失。

## 有证据的最小修正

旧 `Tail.a` 是 RGB 共享支撑标记：一个通道有 carrier 编辑，会让仍以 FP32 tail
传递的中性通道也采用 Feature1 偏差。现在 Compose 使用**同一 recording 的低分
carrier**，只在权重非零且该通道实际 stored carrier 不为 0.5 时采用模型部分。
保留全局 Z==B、有效 tail、区域/alpha/HDR headroom、原 Reset 和生命周期保护。
不改编码 range、不添加光流或外部历史，不新增玩家算法组合。

实际 Feature1 的 640×384 / 320×192 阈值正负控使用同一新 shader：
`per_channel_support=false` 复现旧 RGB 门，默认 true 为候选。16 帧 B/Z、carrier、
tail、depth/MV 和原始 reconstructed 逐帧哈希完全相同；中性通道的模型增量最大值
从 `0.0009765625` 降为精确 `0`，定位到 Compose 门而不是推断模型/Reset 根因。
同帧匹配空间参考使用同一支撑策略。有效 R/G 通道的模型偏差仍未解决。

冻结输入先预热 16 帧、再捕获 16 帧；B/Z、carrier/tail、guides 全部固定、实际
Bind Reset 为 0、前驱连续，输出仍有增量变化。低分空间往返已损失细线，私有
Feature1 又增加偏差。离线 `Z+(private-spatial)` 未证明净收益，因此**不进入产品**；
不会用继续叠加保护保留该候选。4K 的 N=2336×1312 和 99% N=3802×2138
另有受控实际 Feature1 检查，精确结果见 #309 后续执行回执，不借用小网格证明。

固定入口：`experiments/nr-edit/tools/validate_private_sr_attribution.py`，要求显式
提供已审查的本机 compiler/nrchain/SR runtime。`--scenario threshold` 是带符号跨门
实验，`--legacy-rgb-support` 是负控，`--output-size` / `--work-size` 记录真实网格。
`matched-jitter` 只属于栅格同步的宿主实验，不替代生产 normalize 导引或游戏运动门。
当前生产固定 B/Z + 匹配 jitter-pixel 导引、真实相邻游戏、HDR 与整游戏成本尚未验证。

## 快捷键（独立功能）

四项可单独关闭：NR 开关、当前画面采集、文字反馈、保存截图。保留已有合法绑定；
新增 Home/End/Insert/Delete/PageUp/PageDown/ScrollLock/Pause 选项，三组预设及全部关闭。
默认 F6/F7/F8/减号不变；减号截图的主键盘/小键盘别名都受关闭选项控制。
已知 ReShade 重叠只读提示，不保证游戏全局没有冲突；按钮仍可使用。
详见 [短测试单](053_BETA15_TEST_CHECKLIST.zh-CN.md)。

完整 Windows 构建和实际产品采集验证按本批冻结源码执行，最终状态/真实 build SHA
同步 #338/#309/#311。源码/受控检查/安装/游戏验收各自记录；不发布在线 OTA、不合 main。

## 续接前的开发记录（b16bd25a）

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
