# 实际 Output 存储量化诊断（#229）

存储证据按录制时真实 `slot.result_uav_format` 解释期望值，避免将 R11、R10、R8 或 FP16 的合法量化判成错误写入。此改动仅影响期望统计、probe 读值的规范化及证据字段；`ResultOut`、合成数学、采样频率和 NR 调用保持原样。

## 转换依据

微软 [D3D Functional Specification 3.2.2](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#3.2.2%20Floating%20Point%20Conversion) 规定较窄浮点转换向零截断；[3.1.6](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#3.1.6%2011-bit%20and%2010-bit%20Floating%20Point) 定义 R11/G11/B10 的无符号浮点位域和 denorm。[3.2.3.6](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#3.2.3.6%20FLOAT%20-%3E%20UNORM) 定义 UNORM clamp/缩放/最近整数转换，并允许整数侧 0.6 ULP；因此边界不总有唯一合法编码。typed UAV 的实际 view 决定存储转换，不能用物理 typeless 格式代替它。

| UAV view | 诊断转换 |
|---|---|
| `R16G16B16A16_FLOAT` | 保留 `f16tof32(f32tof16(value))`；PRE、POST 均适用 |
| `R8G8B8A8_UNORM` | 对 RGB 按 255 缩放，计算规范理想整数；允许两个结果时 unknown |
| `R10G10B10A2_UNORM` | 对 RGB 按 1023 缩放，采用同一唯一性判断；诊断不比较 alpha |
| `R11G11B10_FLOAT` | 直接从 FP32 sign/exponent/fraction 生成 6/6/5-bit fraction 的 unsigned-float 编码，包含下溢、denorm、负值截零、有限高值截到最大有限值 |
| 其他 | 明确 unknown，不推断写入失败 |

R11 转换不经过 half 编码再舍入；只在最终已确定的 11/10-bit 编码展开时使用 `f16tof32`。UNORM 用两段整数乘法保留 FP32 输入与整数位深乘积的全部有效位，避免 FP32 乘法在边界改判。设精确缩放结果为 `x`，`floor(5*x)-5*floor(x)==2` 时，规范允许两个编码；该样本不进入已知期望计数。此判断来自格式本身，没有统一像素误差阈值。

actual UNORM 读取先还原存储整数，再采用与 expected 相同的浮点展开，消除 typed load 与 shader 除法最后一位的差异。浮点存储的完整 HDR 值进入 fingerprint，替换旧的 `saturate*1023` hash，避免所有高值被压成同一个编码。

## 消费契约

`NrPostStoreEvidence` 新增 `result_uav`、`comparison_known`、`matches_expected`、`actual_nonfinite`、`reference_nonfinite`。PRE/POST getter 仍分开，身份、物理/SRV/UAV 格式仍在实际录制时冻结。

`comparison_known` 仅在格式有明确规则、所有 actual 样本都有有限且唯一的 expected 配对、probe 没有非有限值时为真。此时 `matches_expected` 同时要求 expected/actual 的完整存储 fingerprint、整数 delta sum、max、changed count 精确相等；没有宽泛的绝对误差容差。

该布尔表示当前稀疏样本的 fingerprint 与统计一致，不是整帧逐像素比较、NGX 已运行、SR 已消费 Result 或实际 Present 的证明。即使 HDR identity 因 FP32→FP16 量化产生非零 changed count，expected/actual 仍可一致；changed count 不能当成 NR 效果强度。

`comparison_known=false` 必须显示本帧未知，不能解释成错误写入，也不能把上一帧成功搬给本帧。历史成功可按它自己的完整身份与时间窗口保留。collector 现在会发布 expected_samples=0 的新 actual 证据，因此 unknown 不会静默留下旧成功快照。

非有限情况进一步区分：

- `actual_nonfinite=true`：probe 已直接读到实际 target 非有限，可据此处理实际输出异常。
- 只有 `reference_nonfinite=true`：参考数据不可比较，不能据此断定实际 target 非有限。
- `invalid` 保持原来的 actual/reference 任一非有限的样本数。stats word 30 低 16 位存计数，bit 16/17 分别存上述两标志；65535 的采样上限确保计数不侵入标志。期望非有限、未知格式或 UNORM 模糊边界不冒充 actual 非有限。

buffer 仍为 40 words，根常量数量不变。Passthrough bits 19..21 仅携带存储诊断格式；PRE 的原始 Color 坐标标志仍独立。FP16 POST 增加可选 probe heap，其已有 `CopyResource` snapshot 保持不变。缺少 heap/pipeline 时缺少证据，渲染路径继续沿用原行为。

## 验证与复现

`scripts/validate-store-quantization.py` 冻结并编译完整 production Core，使用实际 WARP typed UAV 存储和独立 CPU 位编码/双精度 UNORM oracle。测试入口追加在未改动的完整 production shader 之后；额外输出只存在于测试程序。

```powershell
python scripts/validate-store-quantization.py `
  --compiler <llvm-mingw/bin/x86_64-w64-mingw32-clang++.exe> `
  --reshade <reshade-source> `
  --source <canonical/core/src/nr_before_sr.cpp> `
  --output <fresh-result-directory>
```

默认执行全部数值扫描和 Core 集成。`--compile-only` 不执行 GPU；`--core-only` 跳过大数值扫描，保留未知格式/typeless 小型 oracle 和 Core 集成。每次输出目录必须不存在，结果包含冻结源文件、逐文件 SHA、fixture/binary SHA、`run.txt` 和 `result.json`。

初始数值扫描验证 25984 个输入、198646 checks：FP16 1254、R8 2566、R10 10246、R11 11918。覆盖精确值、相邻 FP32、ties、normal/denorm 边界、负值、高值、无穷大及 NaN；R11 遍历全部有限 11-bit 与 10-bit 编码区间。所有可唯一确定的编码与真实 UAV 字节一致；UNORM 模糊输入的真实编码也在规范允许范围内。

Core 组经实际 `CreateSlot`、snapshot、Combine、probe、录制/提交、fence、collector 验证 FP16/R8/R10/R11 identity、真实写入和 wrong-target。普通与 HDR R11 的真实写入均得到 known/match，错误目标得到 known/mismatch；非有限 actual/reference 分别验证。另保留 PRE R8 错目标、FP32 HDR identity 量化回归，以及物理 R8 typeless / 实际 UNORM UAV、未支持诊断的真实 FP32 UAV 小型 oracle。

最终补充组 `build/store-quant-final-r5/result.json` / `run.txt`：4528 checks 通过，19 个 POST、3 个 PRE 及 13 个小型 oracle 输入。R11 actual-only 非有限得到标志 `1/0`，reference-only 得到 `0/1`；低 16 位 invalid 计数均为 1，expected_samples 分别为 1、0，两者都发布本帧 unknown。R11 高值正常/错目标分别为 expected/actual `1/1`、`1/0`，匹配布尔准确区分。

大数值扫描记录在 `build/store-quant-oracle-r1`。最终源码与该扫描的全部转换 helper 字节一致；后续只追加非有限来源标志与集成验证。除存储诊断 block 外，完整 HLSL 与 `b54a6961` 一致，记录在最终目录的 `composition-receipt.json`。CPU colour policy 40960 contracts / 164855 checks 及通用 codec fixture 编译通过。

| 最终补充组文件 | SHA-256 |
|---|---|
| `src/nr_before_sr.cpp` | `b2244d846ba33be8cfa3f5a733209a69b597cc88a3cd42a4731f3db2c23cbd04` |
| 冻结 transformed Core 输入 | `70345b4276e1d842aebb48d1d8b14e3f2da6bbcde1e99e5400e94b5899dde759` |
| 最终 fixture | `079ec98edcc68cf892076e6fd8842fe2005f408e5160f2339050de815cde82e4` |
| 最终 WARP executable | `b26c032b5b4703a390387bb4174cecbbdb9e718327941ffbac3bb769af1561c5` |

数值验证使用构造输入和 neural plane；未调用 NVIDIA Feature 18 或游戏，也不声称 debug layer 验证。最终发布仍需主分支的 canonical staging/pin 与整包验证。
