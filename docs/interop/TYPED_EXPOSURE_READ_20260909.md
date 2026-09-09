# 已确定类型的 R8 曝光纹理读取

DS2 的普通 `0.4.7beta` F8 在 781 ms 内观察到 SR 2069→2114，NR/提交
均为 0，7 次参数采样均为 `exposure-texture-unverified`。曝光资源为
1×1 `R8_UNORM`，无 UAV flags；命令状态已准入，模型加载尚未尝试。
这是曝光读取格式的直接拒绝，不是死亡搁浅 1 的命令列表或输出观察问题。

## 读取合同

[NVIDIA DLSS Programming Guide](https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf)
2026-03-31 版 §3.9（标页 25）规定曝光纹理为 1×1，使用首通道，允许多种
格式，R16F 为推荐格式。本修正只增加已经确定类型的 `R8_UNORM` 读取，
使用同格式 SRV 的归一化红通道数值，不把 R8 位模式重新解释为 FLOAT。
`R8_TYPELESS`、`R8_UINT`、`R8_SNORM` 仍然拒绝。

`NrExposureReadFormat` 是唯一视图解释；canonical staging 的准入、Create、
rebind、普通 descriptor writer 和快照重建继续共用它。保留 Texture2D、
1×1、单数组、单采样、mip0、允许 SRV 及设备实际 ShaderLoad 能力检查。
源纹理不要求 UAV。拒绝不得部分替换 slot 引用或重写 heap。

曝光 shader、NR 参数构造、原生 SR 的参数读取/调用/恢复不变。零、非有限值
及极值沿用既有曝光策略；两个 codec pass 在当前资源上对称读取，不改成
历史 CPU 值，也不因标量曝光为 1 就丢弃游戏提供的曝光纹理。

拒绝日志新增 `capabilityQueryAttempted`。值为 0 时 `capabilityResult` 是
未查询的占位值，不是驱动返回。旧报告里的 `0x80070057` 来自此前初始化的
`E_INVALIDARG`，格式拒绝发生在调用设备查询之前。

## 聚焦验证

- 生产访问 adapter：10,773 项 CPU 断言通过，涵盖格式、shape、flags、
  能力位、查询 HRESULT 与是否实际发起查询。
- 同一 R8 准入探针分别编译旧/新生产 helper：旧版在准确准入断言 exit 1，
  新版通过；不是把编译失败记成预期拒绝。
- 15 项 Python/源码变换检查通过，含完整 Core 输入、所有 writer 接线、
  Create/rebind 在保留引用前拒绝、原生 SR 参数与调用代码逐字保持。
- 增加 `validate-codec-gpu.ps1 -ExposureTyped`。复用已有 codec 宿主，编译
  实际 staged `EncodeCS/CombineCS` 和 `EncodeV2CS/CombineV2CS`、完整
  descriptor writer、`WriteExposureSrv` 与 `RebindExposureSource`。
  80 组 R8/R32 对照覆盖字节 0/1/64/128/255、PreExposure 0.5/2、普通及
  快照 heap 重建/rebind；核对实际 FP16 模型输入、最终结果、曝光 trailer
  的归一化/解析/相对曝光值及原生输入和曝光纹理原字节。

本切片的 80 组 R8/R32 对照已在实际 D3D12 WARP 执行，exit 0；输入为冻结
output.2 staged Core 的隔离副本，仅补入本切片的曝光 helper 和查询日志。
集中候选仍需完整 canonical 构建及同源验收。原有 FLOAT 的 NaN/Inf/极值
回归保留在默认 codec GPU 门禁，本次 80 组不替代那部分检查。

构造 neural plane 不等于 NVIDIA NR 或游戏验收。DS2 前后置仍需分别取得
新 F8 的 NR/提交增量和 F6 最终画面结果。当前不宣称游戏通过。

局部编译可使用 `-ExposureTyped -CompileOnly`；执行时使用
`codec_gpu_validation.exe --exposure-typed`。此次执行设置
`NR_VALIDATION_WARP_ONLY=1`；WARP 创建设备失败将直接失败，不能回落硬件。
未设置这个测试开关时保留原测试宿主的硬件回落行为。输入必须是 canonical staging
后的 Core；原始未变换 CPP 不可充当同源 GPU 输入。测试输出保存在忽略的
build 目录，原始用户 F8 和本机路径不进入本文件或源代码导出。
