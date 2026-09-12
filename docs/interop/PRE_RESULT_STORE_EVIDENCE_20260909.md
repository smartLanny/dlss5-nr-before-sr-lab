# PRE Result 存储证据（#203）

PRE 现在可以在实际 Combine 写入 owned FP16 `Result` 后，通过独立稀疏 GPU dispatch 读回同一个 Result。它沿用现有 40-word stats、readback、录制引用及 fence 回收机制；原始 `Color` 只建立 typed SRV，未增加原始 Color UAV。

## 接线和消费边界

- `SnapshotPreStoreEvidence()` 返回 PRE 最近完成的样本；`SnapshotPostStoreEvidence()` 保持 POST 独立历史。两者使用同一 `NrPostStoreEvidence` 结构，`mode` 分别为 2、1。
- 录制 probe 时冻结完整 `NrDiagnosticIdentity`、Feature generation、native-format key、source/result physical 与 typed view format。collector 使用冻结值，不能把已轮换 slot 的格式或资源身份贴到旧 readback 上。
- `frame/config/feature_generation` 保留兼容字段；完整链路匹配应使用 `identity` 和冻结资源信息。PRE `boundary_trace_id` 为 0，不冒充 POST Output 边界或 Present 证据。直接 typed FP32 路径的生产 native-format key 仍可能为 0，应保留 unknown。
- 只有既有采样计划已 arm、实际 Combine、owned FP16 Result、probe pipeline/heap 可用及最多 65535 个稀疏样本时才记录。采样仍是最初三次、配置变更、每 120 次评估。probe heap 分配失败只缺少证据，不改变渲染路径。
- probe 在 Result UAV barrier、UAV→SRV transition 后运行，结束恢复进入前的状态。collector 必须等实际 GPU fence 完成且录制引用释放，才读取并发布。对比双方样本数、invalid、当前链路身份和证据时效后，消费者才能解释结果；旧样本不能证明新帧。

## FP16 量化语义

PRE 的 expected 仅在统计函数内执行 `f16tof32(f32tof16(expected))`，然后计算 expected delta/hash，并再次检查有限性。主合成数学和 `ResultOut` 写值没有更改。POST 保留既有 typed-store 容差语义。

`expected_mean/actual_mean` 是存储 RGB 相对原始 Color 的最大通道绝对差，每个均值样本截断到 1；max 截断到 4095；changed 使用 `1/1024` 阈值。HDR identity 的 FP32→FP16 量化本身也可能令 changed 非零，它不是 NR 效果强度。HDR hash 会饱和，hash 只作补充，不能单独判定写入成功。

## 验证

`scripts/validate-pre-store-probe.py` 编译完整、冻结的 production Core 翻译单元，借用既有 WARP 队列/纹理 helper。新增 `tests/pre_store_probe_harness.inl` 通过实际 `CreateSlot`、descriptor writer、`DispatchCodec`、录制/提交、fence、collector 和资源轮换验证。

最终本地输出：`build/pre-store-probe-r3/result.json` 和 `run.txt`，3782 checks 通过，包含 13 PRE 与 3 既有 POST 用例。

| 用例 | Expected mean | Actual mean |
|---|---:|---:|
| R8 typeless，半分辨率实际写入 | 0.415763 | 0.415763 |
| R8 typeless，半分辨率错误目标 | 0.415763 | 0.000259 |
| FP32 HDR，全/半分辨率 identity | 0.333257 | 0.333257 |
| FP32 HDR，半分辨率实际写入 | 1.000000 | 1.000000 |
| FP32 HDR，半分辨率错误目标 | 1.000000 | 0.333257 |

FP32 HDR 原始蓝通道约 1024.3333，配显式 1024 pre-exposure；identity 正例保留实际 FP16 存储精度损失。错误目标只把主 Combine u1 指向另一个真实纹理，probe 仍读取实际 owned Result，因此没有把预写入统计当作实际存储证据。

同时验证原始 Color 创建时无 UAV 权限、其字节不变、Result 非活动矩形保留、实际 fence 未完成或录制引用仍保留时不能发布，以及 GPU 完成后 slot rebind 不改旧样本身份/格式。R8 typed/typeless、全/半分辨率均覆盖；现有 POST R10 identity、changed、wrong-target 通过。

CPU colour policy：40960 contracts、164855 checks 通过。除 `RecordPostStoreExpected` 与 `ProbePostStoreCS` 两个诊断函数外，完整 HLSL 与基线 `a0971855b554b8f15a376410bbdd1da55768f775` 字节一致。通用 codec fixture 与 POST scaling extraction fixture 编译通过；后者补齐了共享 descriptor writer 要求的 mock 字段。

这次验证使用当前源码经实际 bridge/format/post/exposure 转换后生成的独立 focused 输入，未修改正式 staging pins、版本或队列宿主。它证明 production GPU codec、存储采样与回收链路，使用的是构造 neural plane；不代表实际 NVIDIA Feature 18、游戏 SR 消费或 Present 已验证，也未声称 debug layer 验证。

| 实际文件 | SHA-256 |
|---|---|
| 当前 `src/nr_before_sr.cpp` | `122aa8942a8990066a387fc6cd318fd60d9ce4c3cf719c16937d70ba6d45665a` |
| 冻结的 transformed Core 输入 | `53efa63440a3ca82abc9239d041dd118bfc0b8d367c2916890be2b3c59e335a5` |
| PRE fixture | `dba87a865888ae288160f50604e3e5258fd50a5927ea85098fd4ed2cbe7d8b18` |
| WARP executable | `8a35fda1b4cb1da47ae707f0c3a777779a5595cc99730b7b09902de8e134f51f` |

正式发布仍须由主分支完成 canonical staging、pin 与整包验证；此提交不包含发布动作。
