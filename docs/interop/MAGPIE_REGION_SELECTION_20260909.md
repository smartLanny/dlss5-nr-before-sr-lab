# Magpie选区方法与D的实际需求

## 新一轮源码核对

2026-09-09重新核对上游，experimental及HEAD仍为
[`ac1cc8b0f2efc78323898395cc1336bcbecdc276`](https://github.com/SAOG0721/Magpie/commit/ac1cc8b0f2efc78323898395cc1336bcbecdc276)。
DLSSNRFilter.cpp的blob仍为`dc59eb18085e8863ecea7c41e3eb685a420ab487`，
与此前9824d758相同。本轮额外核对模型控件、效果模块及光流置信，共9个引用
文件的Git blob均与固定上游匹配；没有运行Magpie或复制上游实现进入产品。

NR后处理按候选相对原图的线性亮度差分正负，再放大整个RGB修改量。
其Reflection/Glow是共用方向类别，不是两张独立反射和辉光遮罩。分类发生于
总倍率和SDR截断之后，因此也不能声称所有倍率都不会改变分类。
[方向分类](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L360-L377)。
随后HSL调节改变被选像素的明度/饱和度增量；它不是按肤色选区。
[HSL处理](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L379-L393)。

模型层另接收SkinStructureStrength、UseAutoMask、UICorrection。公开封装没有
将自动脸/UI遮罩传给残差合成；effect和内置preset的AutoMask、UICorrection
默认均关闭，不能用参数名解释某次反射效果。
[模型传参](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L1436-L1442)，
[effect默认](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Effects/DLSSNR/DLSSNR_AI_Filter.hlsl#L119-L144)，
[preset](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/presets/ScalingModes-v0.6.5-experimental.json#L44-L57)。

因此D5完整亮暗的作用范围比旧细分更接近Magpie，但D按自己的亮度/颜色合同
合成，不能据此称两者画面相同。现有宽亮/细亮也不能可靠区分宽反射和辉光。

## 另外三个值得借鉴的方法

| 上游模块 | 借鉴到D的用途 | 不能据此宣称 |
| --- | --- | --- |
| [Bilateral Mean](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Effects/Anime4K/Anime4K_Denoise_Bilateral_Mean.hlsl#L36-L53)的空间与强度相似性 | 原图导引残差邻域；若深度可信，再拒绝跨物体混合 | 同表面的反射与辉光已经分开 |
| [AdaptiveSharpen](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Effects/Sharpen/AdaptiveSharpen.hlsl#L199-L208)的正负软限幅 | 只限制新增增强的过冲，保留模型基准变化 | 边缘就是文字或人脸遮罩 |
| [NVIDIA光流置信](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/NvidiaOpticalFlowProvider.cpp#L68-L90) | 将界内、深度、运动与修改量一致性分别用于历史接纳 | 置信值就是反射概率，或引擎单向MV等同双向光流 |

这些是独立效果/导引模块，不能写成Magpie NR后处理已经采用。其NR通道实际
选择零深度，残差合成未使用深度或运动历史。
[通道选择](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/FrameGuidanceTypes.h#L113-L134)。
双边和锐化的SDR常数、逐RGB窗口与gamma假设不能原样套入D的HDR路径。

## 后续实现合同

以下是结合D需求提出的设计，尚未实现为生产选区或验证画面效果。

1. 固定同次NR的原图O、模型输入P、输出N，先生成区域权重M，再施加增强倍率g。
   调g不能反过来改变M；模型风格或AutoMask改变N时作为另一次实验。
2. 正、负修改量分别作保边分解，宽成分不超过当前像素该方向的总修改，细成分
   为剩余量，宽+细守恒。保留“未判明”区域；宽亮不得直接等同辉光。
3. 深度负责边界与遮挡，MV负责对应点，脸/UI/材质信息负责语义证据，三者不
   相互冒名。零MV、alpha、肤色或边缘阈值都不能直接当成可信人脸/UI遮罩。
4. 先保留D5基准，再对新增增强独立保护和软限幅；所有新倍率中性时精确回到
   基准。需明确旧主强度及辉光等参数的映射，避免同一增益计算两次。
5. 空间选区通过后再加历史；错帧、曝光/模型变更、出界、遮挡或缺少MV时拒绝
   历史。反射随视角改变，即使几何对应正确也未必应该累积。

第一批实际样本应包含细反射、宽反射、辉光、明脸、暗脸和细字UI。用固定的
O/P/N比较中性及增强输出，显示选区预览和未判明区域，记录保护前后的新增项。
要求增强反射时保留宽反射，同时比全正亮度基线更少影响辉光、脸和UI；再用
未参与调阈值的场景验收。构造完全相同图像却贴不同“反射/辉光”标签的案例，
算法必须承认无法区分，防止把外观代理包装成语义识别。

D6已准备可选资源检查与中心内部平面采集入口，为这些比较取得实际输入；
本轮仍保持D5着色器，未新增上述选区算法。[D6范围](DLINE6_LAYER_INPUTS_20260909.md)。
