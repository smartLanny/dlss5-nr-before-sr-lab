# RenoDX NR 对照基线与配置核验（2026-09-04）

本文记录参考插件的身份、实际配置语义与本地测试限制。画质结果另见
[本地游戏画质测试记录](LOCAL_GAME_QUALITY_2026_09_04.md)。参考插件没有可对应
当前发布二进制的完整公开 NR addon 源码；下文严格区分发布信息、静态二进制
证据和运行中 UI／日志，不能把控件说明当作模型响应的证明。

## 1. 两条参考分支

| 分支 | 发布版本／时间（UTC） | addon | 字节数 | SHA-256 |
|---|---|---|---:|---|
| Generic | 4.70 / 2026-09-01 14:10:18 | `renodx-dlss5.addon64` | 1732608 | `D5ADF82EB44B065F4C590AC91FE824BAB07AFEA0EB9F994BDE936710C8593952` |
| ShortFuse | SF 0.51 / 2026-09-03 04:41:23 | `renodx-dlss.addon64` | 2499584 | `85EAE478F1E733E85B247C32469C2B2CC1A1C0DD2AB4AFD7DAC240E619201CEE` |

发布来源为 [Generic 4.70](https://github.com/RankFTW/rhi-repo/releases/tag/renodx-dlss5-4.70)
及 [SF 0.51](https://github.com/RankFTW/rhi-repo/releases/tag/renodx-dlss-SF-0.51)。
截至本次检查，Generic 最新仍为 4.70，SF 是独立的较新实现，不能混称同一版本。

从发布地址读取 ZIP、在内存中展开并核验，本机 `%LocalAppData%\RHI\rdx5`
缓存的两个 addon 均与发布文件逐字节一致。对应 ZIP：

- `renodx-dlss5_4.70.zip`，632174 字节，SHA-256
  `D6E356D01B429AF6288F488A4926C44F1D779A7D4586EE8C79D04D3A09A536E6`。
- `renodx-dlss_SF_0.51.zip`，943785 字节，SHA-256
  `272F885020FBEDD24AC0D134D9D6DB4EEBB49423C32E23056EFE1770E913E2A6`。

RHI 的 SF 安装流程会共同部署 SR／RR／FG／NR／Streamline。为隔离 addon 差异，
本次应单独切换 addon，记录实际加载的运行库哈希，不通过整套升级建立“同运行库”对照。
该行为可在 [RHI 安装服务](https://github.com/RankFTW/RHI/blob/main/RenoDXCommander/Services/Renodx5AddonService.cs)
核对。不要同时加载两套 NR addon。

## 2. SF 0.51：全局配置与预设配置必须分开

SF 的设置描述对象及有序选项表经静态反汇编核验：

| 项目 | 配置键 | 所在节／数值 |
|---|---|---|
| 处理位置 | `DirectNeuralRenderingHookPoint` | 全局；0 Off、1 Auto、2 Upscaled、3 Hudless、4 Present |
| 源编码 | `DirectNeuralRenderingEncoding` | 全局；本轮未推定其数值枚举，通过 UI 验证 |
| UI correction | `DirectNeuralRenderingUiCorrectionMode` | 全局；本轮未推定其数值枚举，通过 UI 验证 |
| 强度 | `DirectNeuralRenderingIntensity` | 预设；默认 1，滑条 0–1 |
| 模型 | `DirectNeuralRenderingStyle` | 预设；0 Model A、1 Model B、2 Model C |
| 连续次数 | `DirectNeuralRenderingPassCount` | 预设；默认 1，范围 1–10 |
| 局部结构 | `DirectNeuralRenderingLocalStructureStrength` | 预设；默认 1，滑条 0–1 |
| 局部明暗 | `DirectNeuralRenderingLocalToneStrength` | 预设；默认 1，滑条 0–1 |
| 人物蒙版 | `DirectNeuralRenderingAutoMask` | 预设；0 Off、1 On，默认 On |
| 皮肤结构 | `DirectNeuralRenderingSkinStructureStrength` | 预设；默认 1，滑条 0–1 |

全局节是 `[RENODX-DLSS]`。上述非全局设置在 Preset #1 下从
`[RENODX-DLSS-preset1]` 读取；选择其他预设时使用对应的预设节。
不能把全部键放在全局节。`DirectNeuralRenderingEnabled` 与
`DirectNeuralRenderingHookPointOrder` 未获得本版本有效配置接口的证据，
不以它们判断启用状态；`HookPoint=0` 本身就是 Off。

本轮首个 CP2077 会话错误地把人物蒙版、皮肤及其他调参放在全局节，导致
UI 仍为 Character Mask On、Skin Structure 1.00。其他控件恰好等于默认值，
不能据此证明这些键已经生效。这组画面保留为无效配置样本，不能作为参数匹配对照。

还需区分“滑条范围”“运行中手填值”和“启动时读取 INI”：SF 0.51 实际的
浮点设置读取函数在解析后按设置 min/max 裁剪。Skin 的 `-1` 放进正确预设节，
启动后仍会变为 **0**，并非默认 1，也不是原样传入的 -1。
强度滑条说明允许手填越界值，不等于启动 INI 越界值能原样保留。
本次不以控件文字推断运行中所有手填路径，采样前读取实际 UI 值。

可用于本轮实际 Skin 0 对照的启动配置：

```ini
[RENODX-DLSS]
DirectNeuralRenderingHookPoint=2

[RENODX-DLSS-preset1]
DirectNeuralRenderingIntensity=1
DirectNeuralRenderingStyle=0
DirectNeuralRenderingPassCount=1
DirectNeuralRenderingAutoMask=0
DirectNeuralRenderingLocalToneStrength=1
DirectNeuralRenderingLocalStructureStrength=1
DirectNeuralRenderingSkinStructureStrength=0
```

启用前后均核实 UI 的当前预设、Upscaled、Model A、Pass Count 1 与人物蒙版。
AutoMask 关闭时不能未经模型验证就声称 Skin 参数完全不参与；我们的匹配样本也
显式使用 Skin 0。原有 Skin -1 默认样本保留，但不标为同参数样本。

## 3. Generic 4.70 的匹配配置与剩余差异

Generic 的配置读取键和 UI 选项表已在二进制中核验：`NRStyle` 0/1/2 对应
Default/Natural/Cinematic；`NRPreset` 0 为 Default，1/2/3 对应 Preset #1/#2/#3。

```ini
[RenoDX.DLSS5]
NeuralUplift=1
NREnableUpscaling=0
NRIntensity=1
NRPreset=1
NRStyle=0
NRAutoMask=0
NRLocalTone=1
NRLocalStructure=1
NRSkinStructure=0
```

`NREnableUpscaling=0` 保持 Generic 的后置 NR 对照用途，不启用它自己的实验超分。
SF 0.51 实际 options 同步路径固定 `DLSSNR.Hint.Render.Preset=1`，运行日志也显示
`preset=1`；原有 Generic `NRPreset=2` 应保留参数差异标记，但下述运行库核验已
排除它在本轮引起不同权重选择，不能继续将它当成画质强弱的原因。

两者的 Feature 18 创建合同仍不完全相同：SF 日志的 `performance=6` 对应其内部
枚举，调用 NGX 时减 1，即 `PerfQualityValue=5`；Generic 固定传 0。
未找到可安全统一这一项的 INI 接口，保留为实现差异。不要为了对照改动游戏原生
DLSS 档位、输出分辨率或强制替换原生 SR。

SF 的 Model A/B/C 与 Generic 的 Default/Natural/Cinematic 可按 Style 数值
0/1/2 对齐，但不能从命名推导模型语义、效果强弱或场景稳定性。

两个参考插件都不应依赖外部编辑 INI 的即时生效。启动前修改后重启；运行中调整
使用插件 UI，核实实际值及成功 NR 评估后再采样。一次只改一个变量。

### HintPreset：已排除当前运行库的网络选择差异

本结论严格限定本机已核验的 `nvngx_dlssnr.dll` **310.8.0.0**，SHA-256：
`E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`。
以下为只读二进制数据流证据，不是可对应发布包的公开源码证明。

- SF 在 RVA `0x77FE8` 通过 `SetUInt` 写入精确键
  `DLSSNR.Hint.Render.Preset=1`，随后在 `0x78088` 创建 Feature 18。
  Generic 在 `0x109BF` 同样用 `SetUInt` 写入 `NRPreset`，在
  `0x10AB7`／`0x10B12` 创建 Feature 18。当前本项目未设置该键。
- 运行库在 RVA `0x18039` 使用 `GetInt` 读取此键；正常受支持 GPU 路径中，
  getter 失败时回填 0，并清除最高位。其后在 `0x21677` 调用权重查找。
- `CG2RFindWeightByPreset`（RVA `0x23A40`）遍历的注册表范围为
  `0xB0D80..0xB1008`、条目大小 `0x288`，**只有一个 preset 1 条目**，名为
  `CC_Control_History_Blend_Quantize_With_Teacher_honest_tench_2026_07_04_22_30_weights`。
  未命中的值明确回落到 shipping default preset 1。

因此，本轮未指定、0、1、2、3 最终选择同一份注册权重；SF 显式传 1 与本项目
省略该键，不构成当前网络选择差异。Hint 实验降为低优先级的合同验证，不能作为
本轮画质修复优先项。此结论不等于其他创建参数或前后处理相同，也不推广到未来
包含多份权重的 DLL；更换运行库后应重新核验。

## 4. 实现证据边界

- 公开 RenoDX main 在本次核验时为
  `66f4a40362cd7840bc0647734c434670539addb0`，仍没有可对应上述 Generic/SF
  完整 NR addon 的源码。与此前 `e5d66cb` 的公开差异只有构建／网页文件。
- Generic 4.70 内嵌的 HDR shader 可静态读出：代理使用 N2 最大通道压缩及 D65
  色域压缩，输出根据原图恢复尺度与色域。不能继续概括为始终使用旧
  `UpgradeToneMap`。公共 N2 数学实现见
  [neutwo.hlsl](https://github.com/clshortfuse/renodx/blob/66f4a40362cd7840bc0647734c434670539addb0/src/shaders/tonemap/neutwo.hlsl)。
- SF 控件说明涉及 N2、独立源编码与漫反射白点；这不是完整 shader 路径的源码证明。
  它的 Auto 把原生 DLSS 输出当作线性 BT.709，是参考实现的假设，不证明所有游戏
  的输入都满足该合同。
- SF 支持连续多次 NR；Pass Count 大于 1 的画面不能与我们的单次 NR 直接比较
  并归因于颜色算法。Present/Hudless 与 Upscaled 也属于不同输入阶段。
- 本项目 r3 的默认 HDR 合成采用受限亮度残差，结构上与参考插件不同。
  更大的可见变化不自动等于更好的细节、肤色或时间稳定性。

### 强度 1→2：控件裁剪与运行库响应必须分开

短路径静态检查确认了以下不同层次的事实：

| 层次 | 直接证据 | 能否说明模型的 1→2 响应 |
|---|---|---|
| SF 0.51 控件 | Overall Intensity 默认 1；描述对象的滑条 min/max 为 0/1 | 不能，只定义控件范围 |
| SF 0.51 启动配置 | 正确预设节的浮点设置解析后经过 min/max 裁剪；因此 INI `Intensity=2` 启动后为 1 | 不能，2 在进入模型前已被插件裁剪 |
| SF 0.51 手动输入 | 控件说明声称手填值可越过滑条范围；本次未完整验证该操作到运行库的所有路径 | 不能拿说明代替实际应用值与模型输出 |
| Generic 4.70 控件 | Overall Intensity 调用滑条时明确传入最小 0、最大 2 | 不能，允许选择 2 不代表画质增量一定明显 |
| 本项目 | 本轮已有真实 `SetFloat(2)` 的调参证据；analysis-8e 阶段采集已完成，neural 的 I1→I2 增量接近 I1 A/A 波动 | 不能借 SF 的上游裁剪解释本项目现象；见下述运行库分支证据 |

本轮读取的 NR 运行库为 `310.8.0.0`、165840496 字节，SHA-256：
`E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`。
仅只读分析磁盘二进制，没有注入、修改运行库或改变模型参数范围。

在该二进制中，`DLSSNR.Intensity` 的 GetFloat 调用位于 RVA `0x1A922` 附近，
写入参数对象偏移 `0xE0`；只有 getter 失败才回填默认 1。成功读取后未见立即
执行 `min(value, 1)`，且 RVA `0x1CF07` 附近仍可见该字段原值被复制到后续数据。
这只证明上述读取／复制路径，不证明后续 shader 或神经网络一定使用所有越界值。

后续结合 analysis-8e 实测，对同一精确 DLL 的 CPU 数据流进一步核验：

| 位置（RVA） | 直接证据及范围 |
|---|---|
| `0x1A3F4` | `DLSSNR.ControlMask` 读取到参数偏移 `0x60`；这是外部控制蒙版，与 `UseAutoMask` 不同 |
| `0x1C9FA`、`0x1D2E5`、`0x1D50A` | 强度融合需求等价于 `Intensity < 1.0 || ControlMask != nullptr`；有限的 1 和 2 在无外部蒙版时均不提出该需求 |
| `0x1D306..0x1D308` | 上述条件为假时退出 fused blend 参数构造，跳过 `0x1D420..0x1D42A` 的强度写入；不是将原数值改写为 1 |
| `0x22548..0x22636 → 0x3F490 → 0x61710` | 已追踪的网络控制参数上传读取 Tone（`+0xE4`）、Structure（`+0xE8`）、Style（`+0xEC`）和人物蒙版派生值（`+0xF8/+0xFC`），写入 kernel 参数块 `+0x8C/+0x90/+0x94/+0x98/+0x9C`；这组控制上传不包含 Intensity（`+0xE0`） |

独立 post 路径 `0x1C920` 也以同一融合需求参与是否 dispatch 的判断，不过颜色
调整、UI 资源或其他辅助目标仍可能触发该路径。因此直接结论是：**无外部
ControlMask 时，1 和 2 不产生不同的已核验网络控制值，也不产生不同的强度融合
需求。**这支持“强度在该路径用于减弱融合，达到 1 后不继续增强”的解释。
它不是所有路径的 `min(Intensity, 1)` 或通用模型 clamp 证明；本轮没有完整追踪
GPU 内核内部 tensor，也不推广至其他 DLL、外部蒙版或颜色／UI 组合。

[analysis-8e 阶段记录](LOCAL_GAME_QUALITY_2026_09_04.md#阶段定位结果)的五组完整样本
使用 SR→NR、3840×2400、Style 0、Skin 0、AutoMask 0、颜色跟随 0；HP0 下两个
I1 样本和一个 I2 样本均在新实例第 64 次成功 Evaluate 取样，实际参数已核验。
固定星头巾人物的 neural A/B RMSE 为 I1 A/A 的约 0.98–1.01 倍，夹克约
1.01–1.02 倍。该结果与上述分支一致，不支持“先明显增强，随后才被本项目合成
抵消”的解释。这里 neural 是 Feature 18 返回输出，不是网络内部 tensor；
统一 64 帧等待未证明稳态，480×300 稀疏采样也不足以判断面部微纹理。

独立、默认 1 的后合成残差增益可以作为后续实验，需明确命名并检查光晕、高光、
肤色和时域变化，不能伪装成原生 `Intensity=2` 已修复。本轮不据此改动生产画质
路径，生产修复优先处理 RE9 状态问题；其余场景仍需独立实测。

## 5. 本轮退出异常与截图输入

2026-09-04 05:40:35（本地 UTC+8），首个 SF 0.51 CP2077 会话在退出时产生了
新的崩溃报告；CrashReporter 进程创建于 05:40:36，报告器日志在 05:40:37 匹配
本轮游戏进程 60000／线程 48356。它不是旧窗口记录。

本地证据目录：

```text
%LocalAppData%\REDEngine\ReportQueue\Cyberpunk2077-20260904-054035-60000-48356
```

`stacktrace.txt` 记录 `EXCEPTION_ACCESS_VIOLATION (0xC0000005)`、读取地址 `0x48`。
仅解析 dump 异常流及模块表可定位异常指令为 `nvngx_dlssnr.dll + 0x147F8`。
这证明本轮退出异常及故障指令位置，不证明 SF addon 的具体根因，不证明运行中
渲染不稳定，也不是可重复复现结论。保存该目录及 `sf051-first-invalid-config.log`，
不将 dump、游戏存档或个人日志提交仓库；本轮没有执行上传操作。

本地 ReShade 配置的 `KeyScreenshot=44,0,0,0`、`FileFormat=1`（PNG）、
`SavePath=.\` 均符合其截图接口。没有发现相应保存失败日志。ReShade 的 Print Screen
输入使用每帧异步按键状态检查，极短的按键是否被采到不能从工具调用成功推断；
`ForceShortcutModifiers=1` 还要求 Ctrl/Shift/Alt 状态准确匹配。
本轮已改用主键盘减号 `KeyScreenshot=189,0,0,0`，应以实际文件出现为截图成功标准。
上述输入逻辑来自本地 ReShade `runtime.cpp` 和 `input_windows.cpp` 的只读检查。

截图记录还需区分 ReShade 保存结果与我们自己的截图入口。阶段 PFM、显示截图、
不同输入阶段和不同时间点不能因为文件名相邻就当作同帧证据。
