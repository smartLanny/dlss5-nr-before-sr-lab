> Historical dev1 design below. Current dev2 adds opt-in single-inference layers and a hard public strength cap of 4; see [BETA_0_5_LAYERS.md](BETA_0_5_LAYERS.md). Historical 8x fixture discussion is not a supported user setting.

# beta0.5-dev1：高强度保护与低成本编辑场开发线

**这是实验候选，不是正式 Release，也不是已经完成的全部 0.5 重大更新。**

跟踪：[#71](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/71)。首批实现：[#73](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/73)。规范集成分支：`next/0.5`；本改动的工作分支：`codex/050-high-strength-guard`。

## 真实基线与 0.4.4 合入方式

开发起点是 beta0.4.3r3 发布线的 `d42a8eb23778bc602d0d638193872d41216c2203`，包含 #61 的 native post-SR snapshot、#67 的受限命令状态准入/反馈/UI，以及 #69 的 r3 冻结。DX11 #46、FG/MFG、Manager PR 没有混入。

开发期间 #70 / #72 完成 main 归一化，采用 [BRANCHING.md](BRANCHING.md) 的 `main` / `release/0.4` / `next/0.5` 规则。0.5 工作分支保留归一化 main `49dd9d4a619a6e73a89ad9d37e3a92a031c57d8d` 的合并祖先与全部 bug-inbox 证据；其代码树与本起点一致。之前临时建立的 `codex/beta0.5` 只作历史过渡基线，**不得与 next/0.5 同时独立开发或发布**。#73 在规范分支就绪后转向 next/0.5。

0.4 继续负责兼容性和崩溃维护。后续 0.4.4 修复先进入 `release/0.4`，依分支政策向 `main` 前向同步，再用独立同步 PR 带到 `next/0.5`；紧急时允许直接维护线→0.5同步，但记录原始SHA与后续main去重。优先普通 merge 保留祖先关系，不反复 cherry-pick 同一批提交，不把 0.5 实验反向并入 0.4。同步不得覆盖 0.5 的版本身份、新配置、shader 和测试；每次核对 release identity 并重跑完整 CI。不要强制覆盖 main 或改动已发布 tag。

## dev1 已实现的运行时范围

仅 Mode2 / `CombineV2CS` 新增 `HighStrengthProtection`：默认 1，可在画质页关闭，关闭恢复 0.4 的放大算法。Mode1 完整后置参考完全不变。0–1 倍仍走原计算，强度 0 和相同 Proxy/Neural 的旁路不变；设置更新不重建 Feature18 history。

现有 0.4.3 已有光照保护不反转残差的限制、SDR 中性轴压缩和 HDR headroom。问题并不是“完全没做保护”，而是高强度把 log ratio 成倍放大后，SDR 目标亮度仍可能超过 1，最终压成白色；额外 RGB 增益分别硬截断，也会改变编辑方向。

### 只限制额外增强，不给整张原图套降曝光

将经过现有 confidence/光照保护的 1 倍编辑视为锚点。设其绝对 log 幅度为 `b`，剩余预算为 `h`，额外请求为 `x=b*(strength-1)`：

```
受保护幅度 = b + h*x/(h+x)
```

`h=0` 或 `x=0` 时直接返回锚点。函数连续、单调、有界，不增加超出请求的编辑，不反转明暗方向。SDR 额外提亮的预算参考原图最大通道的剩余空间；预算不会压低已有 1 倍结果。HDR 不使用 SDR 的 1.0 上限，仍保留 `sourceLuma-fullProxyLuma` 的原始高光能量。

颜色方面保留已有 1 倍 chroma 锚点，对 **整个 log-chroma 向量使用同一放大尺度**，而不是让额外增益对三个通道分别撞上硬上限。这保留的是额外编辑在 log 空间中的方向，不等价于承诺任何色域下的感知色相完全不变。最终的有限值检查、用户 MaxLumaRatio 和 SDR/HDR 色域安全限制仍生效。

新模式通过 `Passthrough` 的 **1024** 位传入，仅在 `v2_combine && high_strength_protection` 时置位；该位表示策略启用，只有 `TransferStrength>1` 才改变计算。现有 31 个 root constants、descriptor 数量、资源创建、queue/fence/retirement、SR 参数替换和原生 guide 合同均不变。没有新增纹理、dispatch、GPU→CPU 等待，也没有绕开未知 HDR 颜色合同。

配置会持久化，纳入 UI 配置 generation 冲突检测；实际 `codec_flags` 同步记录到 F7 stage capture，普通日志记录请求值。F8 core 不创建 GPU 截图。比较前确认 mode、strength、codec flags、工作分辨率和配置 generation 相同。

### 这次不承诺修好的问题

保护不能恢复已经在 1 倍丢失的高光/肤色，也不是修复模型自身的脸部重绘、曝光历史污染或低分辨率噪声。8 倍仍是压力测试，不是保证可用画质。#39/#42/#66 需要同场景游戏捕获验收，不能仅凭源码推断为已解决。未知 HDR 颜色依旧默认暂停；绝不以本改动作为关闭该保护的理由。

## Magpie 的吸收边界与下一批代码

已核对的实际 upstream：`SAOG0721/Magpie` `experimental`，`24f8fd93ef8f4192c9f4d524074c8193c136070d`，文件 `src/Magpie.Core/DLSSNRFilter.cpp`，比上传报告中的 `2d37f8c` 更近。当前代码包含 scale-aware Lanczos2、低分辨率 `PrepareResidual`、Catmull 横/纵重建；最终仍是 SDR `saturate(original+residual)`，residual multiplier 范围为 1–2。

可以借鉴低分辨率计算和重建分工，但不能把它等同于本项目 0–8 倍 HDR/原生 guide/NR-before-native-SR 的已验证解。dev1 的软增强是本项目独立实现，没有复制 Magpie 的 HLSL/C++ 或其 SDR/HSL 合成。上面的公式是当前实现说明，不是上游性能测量。

后续按独立 PR 推进，未打勾的不是 dev1 功能：

- [x] 高强度额外编辑保护、可关闭 A/B、生产 shader 测试入口。
- [ ] #9：异步分段 GPU timing。统计 encode、Feature18、edit、reconstruction/composite 的 AVG/P95/P99；队列私有时间戳、明确 fence、无每帧 CPU wait。没有数据就不宣称提速。
- [ ] #11：低分辨率 log-luma + 稳定 chroma EditField。先保留 Area/Bilinear，只改变“先求 edit 再插值”的顺序，量化它与“先插值 Proxy/Neural 再求比值”的差异。
- [ ] #8：新增 Lanczos2 AA / Catmull-Rom 实验候选。保留 Exact Area 与 Bilinear；旧 Issue 的 Lanczos3/Mitchell 是历史候选，不静默改写历史结论。separable FP16 临时面必须继承现有 fence retirement，纵向重建尽量融合 composite，避免永久完整分辨率 EditField。
- [ ] #10：真实 guide confidence、source-edge containment 和 GPU 曝光跳变门控。未知 depth 不伪造为可信深度，负权重核不直接做不对称 bilateral 修改。
- [ ] #12：Mode1B 仅研究对照。Mode1 完整输出 reference 保留；第三运行模式尚未实现。

输入滤波保留现有颜色合同：已验证 sRGB/linear/HDR 路径使用对应 canonical domain；**ModelEncodedValueDomain 必须继续保留已约定的存储值域，不能凭猜测改成 gamma 解码**。这点以当前 0.4.3 源码为准，不能机械套用早期报告的概括。

## 验证与发布门禁

`validate-beta05-boost.py` 从生产 HLSL 提取标量函数，编译执行边界/单调/零编辑/1倍锚点测试，同时检查 Mode2、配置、history 与 ABI 接线。它不是独立重写一份数学实现。

`validate-codec-gpu.ps1` 在 D3D12 WARP 上执行提取的真实 shader：0/0.5/1 倍开关一致、8 倍 identity/invalid/reset、Mode1 不受影响、SDR 高光保色样本、HDR 原始高光、强颜色、非整数 model extent、source/destination subrect、alpha、F7 ROI/capture 不改变输出。`-NegativeControl` 特意去掉 guard，必须以 `V3_SDR_EXTRA_BOOST_COLOUR` 命中旧版趋白样本；编译失败/设备失败不算负对照通过。

beta050 workflow 继承 r3 的主机回归、所有 shader 编译、资源/命令状态 WARP、静态分析、中英文干净构建和校验和；增加本次数学与正负 GPU 对照。监听 next/0.5 的 PR/push，并暂时接受 codex/beta0.5 的历史过渡 PR。构建产物仅为 **experimental candidate artifact**，不自动创建 Release 或 OTA，不分发 NVIDIA runtime。

构建包 provenance 同时记录实际 checkout/测试 merge commit 与来源 head commit。PR 产物测试的是合并结果，不要把它说成未经合并的 head 二进制；分支名通过环境变量进入 shell，不能把PR元数据插入可执行脚本。

验证等级必须分开：source-level reviewed；host numeric/compile verified；CI verified；WARP verified；真实 NVIDIA DLSSNR/game runtime verified。状态以对应提交的 Actions/PR 结果为准，不能把本文的测试设计本身当作已经通过。

实机至少覆盖 SDR/HDR、0/1/2/4/8 倍、50/67/75/100% NR 工作比例、人物与高频边缘、暗室→室外、DRS、切窗/读档/镜头切换、Frame Generation 共存和长时间运行。重点看额外明暗是否过早变弱、肤色偏移、保护阈值带来的时域闪烁、总链路 P95/P99。Magpie 独立 shader 数字不是本插件端到端性能结论。通过时域与性能验收后，才考虑把新重采样器作为默认。

## 回退

画质页关闭“高强度保护”，或在 `[NRBeforeSR]` 设置 `HighStrengthProtection=0`，恢复本基线原有 Mode2 增强算法。关闭插件/强度设为0仍是原生SR旁路。降级0.4时保留原配置备份；0.4不会识别新键，但不要覆盖其兼容性或安全设置。

## dev3稳定化

见[BETA_0_5_DEV3_STABILITY.md](BETA_0_5_DEV3_STABILITY.md)。0.4.5候选以保留父提交的方式前向同步；0.5分层推荐重置与反馈快照单独补齐，未合并canonical分支。
