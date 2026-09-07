# dev3 稳定化补充

本包保留 dev2 分层算法，并吸收0.4.5候选修复。先读同包 `2077-TESTING.zh-CN.md` 与 `2077-RESULTS.txt`；完整范围见仓库 `docs/BETA_0_5_DEV3_STABILITY.md`。本包未完成真实游戏验收。

# beta0.5-dev4 — 单次 NR 分层强化实验候选

这不是正式 Release，不代表全部0.5路线完成。此候选在PR73的高强度保护基础上实现**一次NR推理、分层合成**；不是OptiScaler PR26的多次模型级联，也不承诺性能无损或已经改善真人脸画质。

## 基线、分支和后续同步

实现基于 `codex/050-high-strength-guard@14957312ad42debd70bd5c8040fc433e05c502e9`，该PR73仍未合入 `next/0.5`。先以独立stacked PR审查本次增量；PR73验收后再同步目标，不自动合并main或release/0.4。

最终效果强度0–4采用 #102 / #112 的同一改动与生产测试；保留cap PR的祖先以便未来0.4→main→next/0.5前向同步。模型内部Intensity仍0–2；MaxLumaRatio是另一项输出约束，不能混叫最终效果强度。旧>4配置在内存归一化为4，下次正常保存遵循原权限/事务流程，不在加载时强制改写INI。

#92的0.4.4-test1、#79 DX11、#111释放失败修复均未被偷偷整包混入。本候选不修改NR生命周期；真正增加独立NR feature之前必须先解决其依赖和失败边界。仅为Windows干净检出保留归档原始字节，采用与#92相同的 `bug-inbox/** -text !eol` 规则，不改归档内容或放宽source-dirty判定。

## 上游到底是哪两种“叠层”

本轮实际读取：

- Magpie experimental：`9824d758b162ad3c5b5acc81e2e14c83f138e13d`，`src/Magpie.Core/DLSSNRFilter.cpp`。`PrepareResidual`在低分辨率上应用明暗方向、饱和度/明度控制，再通过Catmull恢复。其SDR/HSL与最终saturate合同不直接适用于我们的HDR链。
- [OptiScaler_DLSSNR PR26](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/26)，head `Moeblack/OptiScaler_DLSSNR@4417c0ee59c880ae7f9d8f77da43d411dfbaba59`，`OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp`。它在独立feature/history上依次Evaluate，上一层输出给下一层，原始modelInput保留供整链matched residual。INI明确更多层增加GPU工作并可能增加伪影。作者自报GTA V Enhanced切换成功不等于本项目通过，更不是跨场景无损性能证明。

附带的2026-09-05架构报告是较早的重采样/EditField研究依据，不包含此PR26的新实现；其中8倍测试与旧分支名不是本次产品上限/当前基线。实际来源相互区分，不把报告的推荐默认当作已经验收。

本次独立实现的是控制分工与有界数学，没有复制上述项目的HLSL/C++，没有改变输入/恢复核，也未实现低分辨率EditField纹理。后者继续 #11/#84/#85/#86。真正2–3层NR的独立历史/资源/phase/failure研究已单列 #113。

## 新控件与数学

在“分层强化（单次 NR）”中：

| 控件 | 范围/默认 | 含义 |
|---|---|---|
| LayeredEnhancement | 0 / 默认关闭 | 只在Mode2前置合成生效 |
| LayerTone | 0–2 / 1 | 大范围、与中心同方向的明暗修改贡献 |
| LayerStructure | 0–2 / 1 | 剩余局部结构修改贡献 |
| LayerBright | 0–2 / 1 | 原有修改方向为提亮时的系数 |
| LayerShadow | 0–2 / 1 | 原有修改方向为压暗时的系数 |

1为中性，不是一次新的模型调用。颜色仍由原有ColorStrength独立控制；本项不开放或关闭未知HDR颜色合同，不替代#109/#110的独立UX需求。不能把luma控制称作语义脸部/材质识别，不能恢复模型未保留的细节。

令 `e` 为经过已有细节/光照保护的中心log亮度修改，`m`为既有邻域明暗估计。先取同符号部分 `b=clamp(m,min(e,0),max(e,0))`，其权重 `f=abs(b)/abs(e)`（零修改有定义）；再计算：

```
gain = f * LayerTone + (1-f) * LayerStructure
directional = e>0 ? LayerBright : e<0 ? LayerShadow : 1
lumaStrength = clamp(TransferStrength * gain * directional, 0, 4)
```

这个同符号分拆不会由相反方向的邻域均值人为创造暗/亮边。它是有界启发式，不是理想频率分解或物理光照分离。所有系数为1直接返回原master，关闭则保留原计算路径。方向在应用新系数前确定，不因调系数自我改变分类。

只对明暗使用新有效强度；色彩使用原有独立master/ColorStrength与其保护。原高强度软上限对计算后的有效明暗强度生效；HDR原始headroom、最终MaxLumaRatio、finite/gamut、alpha、活动矩形与原生SR保持不变。用户不能把master4再叠出隐藏的8/16倍明暗指数。

## 性能和资源边界

没有新增Feature18 Evaluate、feature、GPU纹理、dispatch或CPU等GPU。复用现有合成pass和光照邻域；若LightingLock=0且LayerTone不同于LayerStructure，新增分层需要计算原先跳过的四邻域估计，因此会增加纹理读取。即使没有这部分读取，也有ALU/分支成本，必须由 #9 实测，不能说完全免费。

shader root constants从31扩为35，前31个偏移不变，新增4个浮点默认中性；descriptor数和资源状态机不变。C++/HLSL布局、所有入口编译与真实WARP绑定必须一起验证。composition-only配置进入SameConfig与保存/事务，却不进入Feature18的history/tuning signature。

F7每帧附带实际codec flag2048与四个提交常量；请求/实际配置也分别记录，不在异步写包时拿新配置冒充旧帧。原像素plane格式和F8核心/图像分层不变。`layeredEffective`表示该次合成启用分层策略，不声称该像素一定发生可见变化。

## A/B、恢复和验收

“仅恢复分层默认”关闭分层并将四项恢复1，不改模型参数、NR工作比例、最终强度或用户的HDR设置。关闭分层后恢复dev1在合法0–4区间的数学；大于4的旧值不再支持。高强度保护仍可单独A/B。Mode1完整后置参考不采用本分层。

先固定同一存档、脸部镜头、R/N/O、运行库和全部模型参数，比较分层关闭/全1中性/只改一项。降低提亮或大范围影调是可做的实验，不是已经证明的“金属脸修复”。按需用F7补画面，F8仍是核心日志。历史0.3的同名数字不等价于当前模型/合成效果。

实现附带的验证入口：

- `validate-strength-cap.py`：共享#112的真实归一化27项与历史8倍负例。
- `validate-layered-composite.py`：从生产shader提取标量函数编译运行；验证有界有效指数、同符号/正负独立、零master、中性锚点、配置/历史/ABI/采集接线。
- `layered_composite_gpu.inl`：生产shader的WARP明暗独立fixture、关闭/中性逐位对照、原始identity/reset/invalid、SDR/HDR、subrect/alpha与F7 ROI一致性。
- `validate-codec-gpu.ps1 -LayerNegativeControl`：故意去掉分层标志，必须准确命中 `LAYER_DIRECTION_ORACLE`，不能把设备错误或编译失败算成通过。

原有高强度8倍shader fixture仍作为直接注入的历史数值压力测试，**不是存在隐藏运行时8倍开关**。所有新公开测试用0–4。完整SM5、资源/命令状态、后置snapshot、parser、静态分析与双语干净构建必须全绿；当前状态看PR Checks，不把这份测试设计本身当作已执行。

**尚需真实NVIDIA/游戏验证**：人脸自然度、肤色/材质、高频边缘与运动、HDR/曝光变化、NR比例变化、切窗/读档/FG共存、基础帧时间P95/P99与显存。不承诺真人脸改善、无性能代价或通用兼容；异常即关闭分层/插件并保留原生链。
