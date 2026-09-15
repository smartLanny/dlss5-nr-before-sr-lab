# D21：AMDOF 优先、防闪、兼容降级与画质控件研究

日期：2026-09-15。关联 #275（本版防闪与预设）、#239/#190（兼容输入）、#274/#104（画质）、#9（开销）。

**本提交是研究与可执行数值复现，不是已接入光流的新 Core。** 不修改运行时代码、用户 INI、Manager 部署、游戏文件或 CI 配置。不得以此关闭功能 Issue。使用 GitHub 源码和隔离的 Linux CPU 标量测试；没有使用 DevSpace，没有进行 Windows、WARP、NVIDIA 或游戏验证。

## 1. 固定基线与现有工作

- 本仓 D21：`cbfb33919696a2840ed5154c91abaea56228b661`，标识 `beta0.5-dline21-223fix2`。读取时 main 是 `dae86e9898399c20e52ada54a5ec4350da4d4fa1`，不能用其反馈提交代替 D21。
- Magpie `v0.6.8-experimental.1`：`3841698348bfb246623d4acf791984c8b68a577b`，2026-09-14 发布。必须读当前 `DLSSNRMultiPass.h` / `DLSSNRTemporal*`，不是旧版交接首页。
- 本仓 #250：`36aeae2b8b8288e861657993995ca6265e00450e`，`experiments/fullres_guides` 原为 guide-only/preview，并没有接 NR。
- 本仓 #256：`4a931d59a812d407c86725fb90b088dc9e05d412`，有 OF→ExternalProvider→OurCore 的 Feeder 适配；应复用其后端与传输，不重做通用接口。
- #256 的 `feeder_adapter.inl` 明确要求同尺寸 RGBA8 Color/Output、R32 Depth 且深度非空；它不是通用 HDR、缺深度或防闪实现。D21 中没有 `experiments/fullres_guides/backend.hpp`。
- #257–#259 不自动合并；#265 的另一种时域/残差 carrier 实验不是本次 OF 防闪的现成实现。

## 2. 本版选定的实施顺序

| 顺序 | 目标 | 完成定义 |
|---|---|---|
| A | 颜色有效值、独立色彩衰减与对照 | 找到代理输入→NR原始输出→最终合成中差距出现的位置，修明确分支问题，不能只加饱和度掩盖它 |
| B | AMDOF 共享服务 + 基础/增强光流累积 | 实际新源帧→光流→重投影验证→多层新增修改稳定→当前最终输出；仅 UI/字段/CPU 表不算 |
| C | Present 无导引降级及 DX11 接线 | 原生优先、缺项补足、无导引仍可尝试 NR-only；失败保留原画面，明确 F8 原因 |
| D | 低频时域重建、双/三层预设、温和锐化 | 与关闭/完整串联对照，运动人脸/暗部/植被不被抹掉，测整帧和显存 |
| E | VK/legacy 部署与可选 XeFG | 复用验证过的 bridge/feeder 配方，不让全部 API 与 FG 私有补丁阻塞 B–D |

这些是维护者本轮要求的实施范围，不表示上述项已经实现。颜色修正、光流算法、传输和 Manager 应按真实风险分开成小 PR；先建立可运行的最小纵向链，不再无限扩展框架。

## 3. 兼容模式：导引缺失不能等于没有 NR

D21 `src/nr_compat_present_models.inl::NativeGuideAvailable` 仍要求 owned/current native Depth+Motion 和合法尺寸/尺度；已有独立 Feature18 执行器，应扩输入来源而不是重写整个执行器。

目标层级：可信原生 → 按缺项使用 ReShade Depth/已有 MV provider → AMDOF（默认估算）/NVOF（可选） → 明确标记 Unknown/OwnedZero 的 NR-only 路径。Depth 与 Motion 分别选择，不因其中一个缺失就丢弃另一个正确的原生输入。

无原生导引与资源无法合法使用是两件事。前者允许画质降级；后者保留正常游戏输出。占位导引需要真实、初始化、当前 runtime 接受的资源，不能把空指针或猜测投影写成已验证原生数据。缺导引 NR 的能力测试必须与已知可工作的原生路径隔离；不透明 GPU/NGX 失败之后不能继续录制或回收其可能仍引用的资源。当前设备已丢失等硬故障也不应承诺能靠插件恢复所有输出。

验收同时要求：(1) 真正没有原生 DLSS/可读深度时能出现 NR 效果；(2) 某个可选 provider、防闪或资源分配失败不以黑帧/旧帧覆盖游戏；(3) 清楚显示“原生导引/估算运动/无深度降级/未处理”。安全旁路不是无导引 NR 的成功证明。原因变化时提示 F8 收集并上传，不逐帧弹窗。

### 最短跨 API 路线

- DX12 原生入口保持自有 Core，不制造额外 DLAA 调用只为唤醒自己。
- DX11 复用本仓 transport/私有同显卡 DX12 工作端；在可验证路径直接扩展输入，不先写第二套 NR。
- DX11/Vulkan 有原生 DLSS且需保留原生资源时，NIGos mirror 可作外部配方；没有原生入口时用已固定版本并验证 OurCore 所有权的 Feeder 适配。
- DX9/32位首版复用 wrapper/host。`addon64` 不能被当成32位进程插件；全新 native DX9/Vulkan 后端不是本版防闪前置条件。
- Manager 管组件版本、依赖、API安装方法、排他配置、备份/回滚；Core 负责当前帧导引/处理/历史。API只是配方条件之一，还要看原生入口、位数、格式、设备、loader与现有FG。
- 可以缓存多套配方，但不能同一源帧同时运行多个 bridge/feeder 和 NR owner。区分普通 RenoDX HDR 插件与 NR 处理器；不要只按文件名前缀禁用无关插件。

## 4. AMDOF 与防闪共用服务，但不是同一件事

自动导引策略仍优先原生；当需要光流时先用 AMD，NVIDIA 是可选后端。可为防闪单独强制 AMD 而不替换 NR 的原生 MV。AMDOF优先是维护者选择与实验优先级，不是已证明它对所有游戏优于NVOF。

每个新 source/reference 帧对计算一次；多个 NR 层与稳定器共享，按各工作网格转换单位与坐标。流是在未被本插件 NR/锐化污染的输入对上求出。必须保留源帧与参考帧身份、view/device、时间戳、分辨率/有效区、方向/单位、颜色曝光域、生产与最后消费 fence。光流内部的上一捕获帧可能不是模型最后一次成功采用的帧，不能仅检查“最新”就接线。

AMD SDK 的8x8块运动输出需稠密化和源网格单位转换；场景切换标志参与重置。Magpie AMD后端固定0.65并非实测逐像素置信度。可在本层做颜色重投影误差、运动边界和可用深度校验；不要把未证实的confidence mask当成Feature18支持的参数。

NVOF不可用须显示原因，不静默换算法。共享光流池需明确请求/实际后端，不重复启动预览addon、集成Feeder和Core的三份计算。预览、F8与正常显示不得互相重置模型或抢占源帧。

## 5. Magpie 0.6.8：三个需要保留的算法选择

各 NR 仍识别完整前级画面，各有独立 Feature/历史。定义 B=第一层结果、F=全部层结果，D=F-B；单层时B取原图。防闪只处理D，最终B+稳定的D。

### 基础光流累积

把历史D按MV重投影，用原始画面的3x3颜色匹配、运动不连续/越界等证据决定权重。不信任历史时用当前候选，不累积黑值。不是对最终整图做普通平均。

### 光流累积+

额外记录“修改的支持度”与“有修改时的幅度”。Magpie通过support让修正出现/消失更连续，源码幅度80ms、出现60ms、消退180ms是其参数起点。相反方向变化开启新支持阶段；历史权重由捕获时间间隔而非FG显示帧率决定。

需要保留优势而不照搬隐藏衰减：其 `observed=smoothstep(.005,.02,max(abs(Guide(raw)-Guide(base))))` 在无历史时也参与输出的support乘法，小幅的真实纹理修改可能被压弱。这是防闪/细节取舍，不据此断言上游bug。我们的 reset/no-history 回退应验证是否需直接使用raw，不能靠删除所有细小NR变化获得低闪烁指标。阈值须在指定颜色/曝光域定义，而不是直接套到scene-linear HDR数值。

### 低频时域重建

将D降为低频观测，以原图引导恢复；只累积低频，当前高频补集直通。它保护当前细节，也会留下部分当前高频噪点，不是万能降噪器。Magpie当前仍保留全分辨率历史/guide，并非全部只有四分之一显存。

基础和+共用大部分重投影/历史设施，优先实现；低频重建增加独立资源与重建测试。菜单互斥选择，不默认层层叠滤波。保留关闭与未经衰减的完整MultiNR对照。

### 工作域与运行边界

模型代理差分、线性显示差分、最终HDR差分不等价。最忠实对照是在相同最终合成规则下生成NR1与MultiNR，再差分稳定；较低成本方案在一致模型工作域稳定后合成一次，但必须作为不同候选验证。不能把饱和、色域投影、HDR还原视为可随意交换顺序的线性算子。

镜切、时间反向、丢失对应参考、frame gap、配置/模型/分辨率代次变化、FG ownership变化时清空或显式转换历史。重复源帧不推进EMA。良好深度是附加拒绝证据，不是光流累积能否运行的硬门槛；HUD不确定时更快采用当前帧。NR或资源失败不能静默展示旧历史。

## 6. D21 的颜色问题：尚未排除，而且有条件性数值证据

生产代码 `src/nr_color_amplification.h::Resolve` 在 protect=true且T>1时输出 `min(C,1)/T`。`nr_color_policy.inl::ApplyRequestedUnverifiedHdrColor` 无条件使用这一预算；但V2独立色彩消费者 `IndependentChromaScaleV4` 不再乘T。`DispatchCodec` 的独立色彩标记来自 `independent_chroma_composition`，Present也有对应标记。

在已允许颜色、独立路径启用、confidence=1、未触及色域上限时：

| 请求C | 总体合成增益T | 生产系数 | 独立消费者scale | 旧耦合消费者的T×系数 |
|---:|---:|---:|---:|---:|
| 1 | 1 | 1 | 1 | 1 |
| 1 | 2 | .5 | .5 | 1 |
| 1 | 4 | .25 | .25 | 1 |

这违背独立色彩不随非零亮度增益改变的语义。**不是说所有D21配置都命中：独立色彩默认关闭。也不是说旧耦合路线的有效系数0.5就必然只留下50%颜色，shader可能乘回T。** LowGridColor等不再乘T的消费者也需按实际recorded路径核对，不能只看请求开关。

建议修复：预算显式接收实际消费者类型（耦合/独立/low-grid独立/legacy mix），保留耦合路线原合同、HDR准入、finite/gamut/ratio和master-zero保护。不要全局关闭ColorProtection，不把检查g_config的零散特例当长期方案。修改涉及生产header、policy、DispatchCodec、Present Codec与实际调用测试，而非只增加一个无人调用的helper。

### 可运行复现与验证范围

`tests/d21_colour_budget_probe.cpp` 编译真实预算头；独立scalar为对应HLSL三行的CPU转写，不是GPU执行。

```sh
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/d21_colour_budget_probe.cpp -o colour-probe
./colour-probe --expect-d21
./colour-probe --require-independent
```

D21结果：53个现有行为检查通过，9个独立不变性样本失败。`--expect-d21`退出0只证明已复现现状；`--require-independent`有意退出1表明独立语义不成立，不是已修复测试通过。此次云端使用与GitHub blob `6ed7ff9f9b86cbeafed6711f9fe90d602f1497bd` 完全一致的1422字节生产header，GCC严格C++17与Clang ASan/UBSan均复现同样结果。不是完整Core编译、CI、shader或游戏验证。不要把此诊断直接加入自动CI作为正常通过门禁。

### 为何不能把纹理差距全算到颜色上

未验证HDR会在policy令C有效值为0；与上面1/T衰减是两个问题。LightingLock、EdgeGuard、近黑置信度、工作分辨率、实际采用层数、模型Create参数和色彩代理都能独立影响纹理。HighStrengthProtection和ColorProtection不是同一开关；前者是否处理>1增幅也不代表后者或近黑/边缘控制没有起效。

RenoDX/Opti的paper-white改变送入模型的亮度归一化/代理分布，因此可能同时改变模型输出的颜色和纹理，不是末尾普通饱和度。不同插件的倍数、nits、游戏预曝光尺度不能直接抄数值。应比较相同模型DLL与Create参数、每层真实尺寸/采用计数、相同帧序列/历史/曝光下的：原图、送入模型代理、NR1原始输出、最后NR原始输出、最终合成。如果差距在代理前，先修输入；在模型后，查参数/导引/历史；只在最终合成，才查保护/合成。没有这组实测不能宣布网友纹理反馈只是更暖或更锐。

## 7. 显存：确定的分配账与可验证优化

`nr_dual_spatial_d3d12.cpp::CreateSpatialSlot`：普通输出RGBA16F=8B/像素；首次降采样另有RGBA16F输入、R32_UINT有效性和RGBA32F残差；末层累计另有RGBA32F残差。`RecordSecond::MakeOutputReadable`在delta存在时就dispatch残差。主层/原图借用引用不等于新分配，不能重复计费。

| 辅助SpatialSlot角色 | 仅这些纹理的逻辑字节/辅助像素 |
|---|---:|
| 同尺寸直接辅助层 | 8 |
| 首个降采样辅助层 | 36 |
| 后续末层累计残差 | 24 |

1920x1080辅助网格的一张RGBA32F约31.64MiB；若8个工作槽都有副本，仅这一平面上限约253.13MiB。这是尺寸算术，不是测得占用，且没有计对齐、guides、主层、NGX内部或退休代次。

优先顺序：
1. 完整>=3层只采用最后累计delta时，按消费者需求取消首个local delta的常驻分配/dispatch。先查双层回退、F7/F8、区域描述符等读取；将hasReducedInput与needsResidualOutput分开，不能直接删delta，因为现有代码用delta指针判断reduced。
2. 按实际在途fence与消费者需求懒建/复用槽，减少多余退休代次峰值；不能强制8改2造成同步阻塞，也不能因为CPU超时就释放NGX可能引用的资源。
3. 同一有序DX12链共用一次输入准备和已证明可共享的guide转换；不要每层来回跨API/CPU等待。
4. 在量化误差、强度放大、HDR尾部、近黑和无改动恒等测试之后，再试FP16或缩放编码delta；保留FP32计算/诊断参考，不盲目全转8位/半精度。
5. NR每层内部历史独立，不能用同一个Feature每帧连续调用冒充多层。显存不足时可明确降到可用方案，不静默显示请求层数为已采用。

RenoDX缺少与测试二进制对应的公开源码，不能声称已证明它用了特殊权重共享、零复制或更少实际显存；当前优化依据是我们的真实分配与公开独立历史机制。

## 8. 两组预设与锐化

实验起点：双层均衡=主层100%+第二层75%；三层画质=主层100%+后两层75%，再对照67%与100%。比例相对主层，不是显示分辨率。主层本身也可由用户降低，但不得在预设中隐藏乘法关系。

名义推理像素量分别1.5625和2.125个主层网格；五层辅助50%约2.0，与两层全尺寸同量级但时域、语义、固定成本不同。这不是耗时线性预测。优先实测两层清楚输入是否胜过五层小图，不默认层数越多越好。已运行的后层只降低混入权重不节省其推理成本。

Magpie有独立CAS效果，源为AMD CAS；没有据此证明用户所指观感只来自某个NR内部自动锐化。建议直接参考有明确许可的AMD CAS实现；在低分预设启用一次温和锐化，安排在防闪之后、输出编码与UI约束正确的位置。CAS要求正确线性颜色转换；不向PQ码值或负残差直接套SDR锐化。前置NR若在游戏SR前锐化，需要另测闪烁和jitter；首选输出端可证明的锐化位置。锐化不是降噪，不能补回模型没看见的真实纹理。用户关闭或改为手动后不得被预设每帧覆盖。

## 9. 简单菜单，真实有效的高级控制

主页：处理位置、单/双层均衡/三层画质、总效果量、防闪模式；折叠中显示实际尺寸、层数、导引来源和耗时。模型风格与光影预设是不同维度。

高级：AI色彩跟随、总饱和度、原图亮/暗区域饱和度、广域/细小NR修改的色度贡献、原图光照保留、边缘保护、主层贡献、后层新增贡献、锐化自动/手动/关闭、OF来源与分辨率。主层/额外贡献在同一颜色域实施，总幅度仍有数值/色域保护。

控件审计：
- AI色彩跟随仍有真实消费者，但可能被HDR许可置0或上述预算衰减；必须显示请求、当前生效及原因，不能统一宣告无效或已排除。
- 普通光影的原样/自然/少辉光是合成偏好，不是更多推理；自然预设会把颜色置80%。不以此替代双/三层预算档。
- 普通光影与GenericResidual/RegionComposition互斥，按真实作用路径禁用不生效组；Present无引擎区域/独立face mask时不可把零数据解释为分区支持。
- DetailStability是现有空间保护，不应换名冒充新光流防闪。
- 引擎Bloom、真实材质分割、游戏原始明暗区、NR增亮/压暗方向、广域/细节频段是不同概念；不要用含糊标签混用。
- 模型参数与输出合成分开；重建模型历史的参数必须显示等待/生效，UI correction未绑定真实UI资源时不能承诺有效HUD保护。

## 10. 原始层减弱与残差增强不是无损

同一工作域令R=N(X)-X。`a*X+b*R`在NR无改动(R=0)时等于aX；a!=1已经改变原图。`X+k*R`不额外推理，但k>1同时放大模型的好修改、噪点、伪影和可能的色域越界。

更清楚的高级实验是 `X + a*(NR1-X) + b*(MultiNR-NR1)`，默认a=b=1恢复对应完整候选；b=0恢复NR1。它让用户测试后层增益，但不同分支的非线性合成/量化必须明确定义，不能以此保证视觉无损或编造新模型。可调强度与耗时开销是两件事。

## 11. XeSS FG/MFG：选配，不抢本版防闪

Intel当前XeSS-FG开发指南1.3明确：非Intel GPU最多生成一帧（2x显示倍率），要求DX12/SM6.4和XeLL。不是所有显卡官方都有3x/4x。Magpie `XeSSFGCompatibility.h` 的额外倍率兼容路径对特定libxess_fg版本/哈希进行内存补丁并接管pacing/scheduling；不能只传一个倍率就视为通用能力。

已有可借鉴后端包括OptiScaler的XeFG/输入输出分离，但它是中间件集成，不是已验证可丢进所有游戏的ReShade addon。Manager可优先提供固定版本的可选配方，先接官方2x能力；非Intel多倍插帧另列显式实验，不默认升级未知runtime。保留已有原生FG优先。用户要求XeSS替换时，需排他停止/销毁原FG、建立新呈现owner并有失败回退；不能仅检测到DLL就直播热覆盖。初期选provider/安装重启放Manager，已初始化并由我们独占的provider开关可放游戏面板；是否可热切换由实际swapchain生命周期决定。

NR尽量在真实源帧且FG之前完成；不要重新NR每张生成帧。无导引NR模式不自动等于可用FG导引。HUDless/插帧UI、源帧编号、延迟标记、HDR格式、呈现步调都要独立验证。

## 12. 一次收齐的验收矩阵

同DLL/同模型/同尺寸/同真实帧序列：单层、完整双/三层、累积、累积+、低频、低分+锐化；覆盖静态和运动脸部、暗部、植被细线、火焰/反射、HUD、遮挡和镜切。测试原生/估算/占位导引、resize/Alt-Tab、HDR曝光变化、FG开关。

至少记录raw代理/各层输出/最终输出、每层实际采用、颜色请求/生效/理由、对齐来源/参考年龄、单阶段GPU毫秒与整帧median/P95、资源峰值与退休量。不用静止截图SSIM或显示FPS代替连续画质与性能。验收失败保持Issue open，并给F8指向，不能只写“source-level verified”就变成游戏成功。

## 来源

- 本仓D21：[Core](../../src/nr_before_sr.cpp)、[colour policy](../../src/nr_color_policy.inl)、[budget](../../src/nr_color_amplification.h)、[Present](../../src/nr_compat_present_models.inl)、[full chain](../../src/nr_full_runtime.inl)、[spatial allocations](../../src/nr_dual_spatial_d3d12.cpp)、[light controls](../../src/nr_light_style_ui.inl)。
- [Magpie 0.6.8 release](https://github.com/SAOG0721/Magpie/releases/tag/v0.6.8-experimental.1)；[serial chain](https://github.com/SAOG0721/Magpie/blob/3841698348bfb246623d4acf791984c8b68a577b/src/Magpie.Core/DLSSNRMultiPass.h)；[temporal shader](https://github.com/SAOG0721/Magpie/blob/3841698348bfb246623d4acf791984c8b68a577b/src/Magpie.Core/DLSSNRTemporalShader.h)；[temporal resource/state](https://github.com/SAOG0721/Magpie/blob/3841698348bfb246623d4acf791984c8b68a577b/src/Magpie.Core/DLSSNRTemporal.cpp)；[AMDOF](https://github.com/SAOG0721/Magpie/blob/3841698348bfb246623d4acf791984c8b68a577b/src/Magpie.Core/AmdOpticalFlowProvider.cpp)；[XeFG compatibility](https://github.com/SAOG0721/Magpie/blob/3841698348bfb246623d4acf791984c8b68a577b/src/Magpie.Core/XeSSFGCompatibility.h)。
- [本仓#250](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/250)、[本仓#256](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/256)；[NIGos](https://github.com/NIGos/dlss5-bridge)、[Feeder](https://github.com/jlrouzies-fr/DLSS5-Feeder)。
- [Opti DLSSNR shader](https://github.com/Dagherbou/OptiScaler_DLSSNR/blob/973761621353b99bee3dc7d4bb27b117fef2644f/OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl)。
- [AMD OF](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/optical-flow/)、[AMD CAS与颜色域](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/contrast-adaptive-sharpening/)、[XeSS-FG guide](https://github.com/intel/xess/blob/main/doc/xess_fg_developer_guide_english.md)（读取blob `d9842701f9eec15167c19d2c6b0fcc31ca0623ca`）、[OptiScaler现有XeFG说明](https://github.com/OptiScaler/OptiScaler/blob/master/README.md)。
