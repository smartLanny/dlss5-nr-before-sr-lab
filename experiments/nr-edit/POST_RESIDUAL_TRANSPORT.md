# 后置最终残差：GPU 传输基础

这是独立实验中的两个实际 GPU 入口和 DX12 包装层，尚未接入 Core 的
`FinishAfterSr`，也没有新增私有 SR 开关、NGX 调用、深度或 MV 资源。

## 输入及调用顺序

- `B` 是未经 NR 增强的原生重建底图；`Z` 是当前合成器已应用最终控制的候选。
- 两图必须是同一真实源帧、相同明确 SDR 编码域的 RGBA16F 活动区域。
  `Domain::SdrEncodedFp16` 和 `same_encoding_verified` 是生产者提供的证明，
  不由资源格式猜测，也不借用旧实验的 `scene_linear_verified`。
- `Encode` 对每个工作像素的完整面积足迹计算 `Area(Z-B)`，不能用模型 `N-P`
  代替；输入子区可处于不同 atlas 位置。N 每轴限于 O 的 25% 至 100%，
  按实际整数尺寸判断，不重新应用一次 WorkingScale。
  这里严格要求 `work * 4 >= full`；Core 菜单 25% 经向下对齐可能略低于
  这个实际尺寸门槛（例如 257 → 64）。这种形状当前会拒绝，尚不能声称
  所有 Core 的 25% 已可接本模块。接 Core 时必须明确解决尺寸合同，不能
  默默重算模型比例或把未接入的模块当作已完成私有 SR。
- 输出为 RGBA16F 中性载体和 RGBA32F tail。最近偶数编码、夹持及量化尾差、
  tail alpha 的无编辑/有编辑标签与旧 Deferred 共用同一 helper。
- 调用方完成一次匹配该载体的重建后，以原 source、recording 和 encode token
  调用 `Compose`。本模块不把一个布尔成功值解释为真实 NGX 已经过验证。
- `Compose` 只计算 `B + decoded_reconstruction + spatial_tail`，保留 B 的 alpha。
  不再次作用颜色、强度、照明、人脸或分区控件。

目标必须事先包含 Z。B 与 Z 资源必须分开，目标不能覆盖 B、输入载体或同次
dispatch 的任何读取资源。目标可以是先前 Z：Encode 的读取完成状态转换后，
Compose 才单独写入；通用录制器为每次 dispatch 使用不可变描述符，执行转换和
UAV barrier，并恢复调用前状态。调用方仍负责原生命令状态恢复、实际提交回执和
recording 退役；不能仅凭 AddRef 重用资源。

## 失败与数值边界

未知域、不同源帧、没有编码证明、超范围子区、非 FP16 图像、错误 tail 格式、
低于 25% 的工作尺寸、读写别名、不同重建 token、没有当前候选、重建失败均在
录制前拒绝。输出子区以外保持原值。

非有限 B/Z tap 使相应低分编辑无效；非法 tail、非有限重建或加回结果使相关输出
像素不写入，保留已有 Z。若 Z 本身已有 NaN/Inf，本模块不会修复它，也不会把它
传播到其他有效像素。中性载体的 SR 偏移被当前支持标签阻断，微小编辑仍保留
在 FP32 tail；这不等于对尾差做了时域重建。

面积边界和恢复像素中心采用整数有理数计算，避免奇数尺寸/99% 下理论零权重
tap 被浮点舍入变为正权重。首次 NVIDIA 检查实际发现这一问题；保留其失败回执，
没有使用 epsilon 丢弃小编辑或忽略有效 tap。

## 验证入口与范围

```text
python experiments/nr-edit/tools/validate_post_residual.py --compiler <clang++.exe> --output <fresh-private-directory> --nvidia --generated-core <frozen-generated-core>
```

脚本严格编译八个 SM5 入口并检查反射，执行旧六 shader 的实际 WARP/NVIDIA
像素回归、私有生命周期 CPU 检查和可选旧完整生成源码 staging 检查。
新传输每种 GPU 执行 60 场景：25/33/50/75/99/100%，129×97 奇数活动区域，
不同 atlas 子区，零编辑、正负微小编辑、正负空间图案、载体夹持尾差、B/Z 的
NaN/Inf 和非法重建值。检查实际存储的载体、tail、最终像素及 B 不变；同时覆盖
目标复用 Z 与独立目标、候选保留和合同拒绝。场景中的上采样载体是明确的解析
测试输入，**不是 NR 或私有 SR 推理**。

不宣称 HDR、R8/R11 最终量化语义、真实模型导引相位、1–5 层运行时接线、
移动/遮挡质量、游戏、性能、零编辑的历史过渡或逐像素最终控制边界已验收。
Core 接线必须继续提供真实 B/Z 来源、最终层选择、原生调用一次及生命周期证明。
