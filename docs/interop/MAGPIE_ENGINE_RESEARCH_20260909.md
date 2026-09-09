# 0.5：借鉴 Magpie 并验证引擎信息的用途

维护者已确认合并候选 D4 的异环 RR 通过，并要求继续学习 Magpie 的其他思路，
发挥引擎集成优势。当前游戏仍使用已验证的 D4；此研究分支首先实现独立 CPU
原型。产品源、模型参数、RR 路由和命令生命周期未改变。

## 固定源码与值得借鉴的部分

对照的是 SAOG0721/Magpie experimental 的固定提交
[`ac1cc8b0f2efc78323898395cc1336bcbecdc276`](https://github.com/SAOG0721/Magpie/commit/ac1cc8b0f2efc78323898395cc1336bcbecdc276)。
其 DLSSNRFilter.cpp 与此前记录的 9824d758 使用相同 blob。下面是对既有实现的
进一步分析，不是新版本算法发布。下载源码仅用于阅读，未把上游实现复制进产品。

| 思路 | 源码依据 | 0.5 的处理方向 |
| --- | --- | --- |
| 缩小输入，只恢复模型修改量，保留原图底图 | [输入核与恢复](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L159-L223)、[残差恢复](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L408-L507) | 现有 ExactArea/Lanczos、低网格修改量与 Catmull 已覆盖部分思路；需要固定模型结果比较边缘混色和成本，不能仅凭算法名判断优劣。 |
| 按整个亮度变化方向调节，而非只调细亮 | [方向控制](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L360-L404) | 新增独立全正/全负亮度原型，隔离覆盖范围；先不引入 RGB/HSL 变化。 |
| 导引必须匹配帧、区域与尺寸 | [资源合同](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/FrameGuidanceTypes.h#L64-L134) | 把同帧、同子矩形、同网格、来源可信作为深度实验入口；不是有一张深度图就可以采样。 |
| 将合成调节和模型历史分开 | [更新与 Reset](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L1783-L1835)、[同输入复用](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L2145-L2179) | D 已分离模型 tuning 与光影系数。继续测实际响应成本，不把屏幕采集去重直接套到 Present/FG。 |
| 保留原 alpha，并限制可接受输入 | [SDR 输入合同](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L1874-L1900) | D 保持亮度/色度分离及 HDR 保护；不能把 SDR HSL 或 0–1 截断用于未知 HDR，也不能将 alpha 当人脸或 HUD 遮罩。 |

## 引擎优势的实际边界

Magpie 这条 NR 路线最终明确选用零深度，可选运动来自光流。
[通道选择](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/FrameGuidanceTypes.h#L113-L134)、
[NR 请求](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L1745-L1749)。
我们能在受支持路径取得引擎深度、运动、抖动和曝光，因此可以研究边界约束和
时间一致性，但尚不能宣称 D 合成已经使用这些导引。

对 D4 实际生成源核对后的边界如下：

- D 合成 t0/t1/t2 已有原图、模型输入与输出；有条件使用曝光及 SDR/HDR 颜色合同。
- 深度已送 NR，RR 深度转换有独立描述符堆；D 的 t0–t7 没有深度槽。
- t3 声明和可选运动重采样不能证明 D 有运动数据；D dispatch 的运动尺寸为 0。
- `NrGuideHistory` 是映射签名，不是残差像素历史。没有 D 前帧深度或残差纹理。
- 没有可供 D 读取的人脸、HUD、材质 ID、法线、粗糙度或独立反射缓冲。

对应源：[描述符](../../src/nr_codec_descriptors.inl)、[合成与参数绑定](../../src/nr_before_sr.cpp)、
[RR 深度适配](../../src/nr_rr_depth_adapter.inl)、[导引合同](../../src/nr_guide_contract.h)。
RR 当前转换也不推断米制单位，不能把其硬件深度输出直接当作线性 view-Z。

## 已实现的第一组独立原型

`experiments/directional_luma.h` 和 `scripts/compare-directional-luma.py` 将
D4 实际生产标量提取出来，与独立全方向亮度候选比较。提取的标量必须与构建源
`ec338d7d9d69230f208b25e4fd457cc79d03c465` 相同，否则拒绝运行。
固定原始修改量、受保护后的修改量、主强度与颜色边界，检查中性位保持、
两方向隔离、零残差不复活、符号不反转和 4 倍有效强度上限。

主强度 1、两个方向控制 2、宽亮/宽暗为 1、辉光为 0 时，解析预期为：

| 已知宽成分占比 | D4 有效倍率 | 全方向候选倍率 |
| ---: | ---: | ---: |
| 0% | 2 | 2 |
| 50% | 1.5 | 2 |
| 100% | 1 | 2 |

这只是明确的参数作用范围差异。全正方向也会放大宽辉光；如果继续将 Glow100
作为“删除全部宽亮”，它仍会删除宽大真实反射。仅凭空间宽细不能同时保证识别
两者，所以此原型命名为方向亮度控制，不称反射识别，也不直接替换当前菜单。

深度原型独立验证同帧、同网格的正 view-Z 是否能减少跨前后景的残差混合。
无可靠深度或映射身份时保留传入的基线结果；不是把 NR 内部状态当成新导引。
深度边界也不能区分同一表面的反射和辉光。

实现为 `experiments/depth_guided_residual.py`，保留 0.40 中心及最多四个
0.15 邻域权重，深度只能降低邻域权重。输入仅接受显式构造的同尺度正 view-Z；
不是米制推断，也不接受零深度、硬件/NDC/逆深度冒名。0.02 相对差阈值只是
实验参数。这里是四邻域宽亮估计的 CPU 模型，未实现实际低网格 GPU 上采样。

已完成的验证：

- 方向原型：4215 项数值断言通过，六行固定输入对照符合上表。
  移除上限及移除方向响应的两个错误候选均先成功编译，再被数值断言拒绝。
  回执候选 SHA256 `3c1500b0e9ac88afb0606ddff675639ac2ac9edab2ef434f529d0524d86b9e88`；
  运行器 SHA256 `18be757f93f0f9932181c76a2c973a32b0126ea55b673413064ab05bdf45464f`。
- 深度原型：12 项测试通过，包括64组同平面精确保持、64组权重/凸包检查、
  两平面深度整体乘10不变、跨面残差更改不影响本面结果，以及身份/编码/尺度/
  网格/有效性不合格时的基线回退。原型 SHA256
  `a40c8be3ddbd9a436bcdcfe7526d9288020d5561863ba0a000de34e1771503eb`；
  测试 SHA256 `799a98c3be48b303a34f9372133236e6dc01ea5071932d68ffff1053aaa6a3a9`。

“同平面精确保持”指这个 Python CPU 基线的数值，不是生产 HLSL/GPU 像素逐位
相同。硬阈值的运动响应尚未验证。可用 Python 3 和 C++20 编译器重复运行：

```text
python scripts/compare-directional-luma.py --compiler path/to/clang++ --output ../directional-result.json
python -B tests/test_depth_guided_residual.py
```

所有结果都是构造数据的 CPU 实验。它们没有实际执行 Magpie、NVIDIA NR、
生成 GPU shader、完整 HDR 合成或时序处理，不代表画面、脸部或性能已经改善。
私有回执保留实际运行环境、源码哈希与逐项输出；提交时再次核对测试文件哈希。

## 后续接入与验收

下一项可接入工作是取得同一次模型调用的原图/代理/NR 平面及其身份，在固定
曝光、模型、工作比例下比较方向覆盖和保护衰减。当前 F7 全屏截图不是这些
内部平面，不把它当成已具备的输入。

之后为 D 合成增加独立深度入口，证明网格、子矩形、深度约定、同设备和
slot/fence 寿命；导引缺失时回退。空间效果确认后再增加前帧残差/深度和 MV
重投影，覆盖遮挡显露、切镜头、曝光与 DRS 变化。时域反射具有视角依赖，
需要单独验证，不盲目累积。每一步都以 D4 已通过的 RR 路线作兼容基线。

“更好”的验收必须同时包含：控制有可见且可解释的响应、反射增强不造成脸部
和整体光照漂移、物体边缘少串光、运动少闪烁，以及实际 GPU 成本。当前只完成
其中数学合同与空间导引的前置验证。
