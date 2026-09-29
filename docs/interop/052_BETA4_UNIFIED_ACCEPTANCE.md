# 0.5.2 Beta 4 统一候选与验收

源码候选 `85b59bdc89a23db14f06d911e83a26a385af351c` 已冻结，r6 完整统一构建与本页列出的软件功能验证通过；双语统一候选已交付，异环已部署并准备回退。受控成本已记录，稳定低开销、整游戏帧 ≤3% 和游戏画质/稳定性尚未验收。不合 main、不发布 OTA，Beta 1/2/3 保留原身份。

## 功能、准入与证据

| 能力 | 已接线与准入 | 已有验证 | 尚待/不包含 |
|---|---|---|---|
| 原生 MV 外部累积 | SR 后置；原生读取边界生成同源 N 网格 depth/MV，保留原始单位与已证明相位；未知来源、单位或相位拒绝 | 导引 CPU/WARP；r6 实际 Core 路由；独立 GPU 深度/边缘/细线反例 | 游戏；RR 外部累积仍仅 AMDOF，不把私有 SR 的 RR 导引当作本路线已接通 |
| AMDOF 外部累积 | 原生 SR 前/后、成功原生 RR 后；全链或可用的后续层作用域；普通与低频互斥 | GPU 真实采用/拒绝/暖机统计、SDR/HDR/Reset/退休及质量反例；r6 原生对象 Present 普通/低频各 9 场真实 NGX 通过 | 游戏与稳定性能；合法内容源跳号误拒已修，旧失败和混合单场保留为诊断，不能替代 r6 正式回执 |
| Present AMDOF | 合法最终 B/Z 的真实来源、队列、录制/提交/退休；默认关闭 | 来源传输 WARP 1700 项；r6 Kio 普通/低频各 9 场实际采用历史；关闭时域的 Kio、20/30 两运行库各 9 场真实 Feature18 基线 | 游戏与稳定性能；拒绝 D3D11、HDR、FG、多交换链和仅有近期原生回执 |
| 私有残差 SR | 成功原生 SR/RR 后完整最终 B/Z，只调用一次私有 Feature1；Flags75、已证明低分辨率 MV 的动态原生 jitter、同域 FP16 原生 HDR/AutoExposure；不重复曝光 | r6 实际 Core WARP/显卡各 17 模式；最终真实 NR/私有 SR 9 场（1–5 层 50%、1 层 25%/100%、RR-only、RR+5 层）均通过；controller 77 项通过 | 游戏验收；32 帧受控成本已记录，拒绝 Present、FG、多 GPU、未知相位/编码、与外部累积同时请求；不自动改保存设置 |
| 省电/恢复 | 两项独立、默认关闭；真实 pause/源帧事件，恢复历史重置 | 生产适配器及暂停/恢复 CPU 回执 | 游戏 Esc 行为未证实；按键本身不是暂停证明 |
| NR 新增噪点处理 | 先验证运动与累积；B 保持不变，只处理 NR 修改量；保护文字、边缘和有效细节 | 两时域模式均匀修改/细线/反号/颜色边缘/深度变化 GPU 正反例；受控 4K 成本八场均功能通过但时间多峰 | 未取得真实游戏噪点收益、稳定低开销或整帧 ≤3% 结论；没有空的 ResidualDenoise 菜单；私有 SR、更快 DLL、旧 NearBlackChromaGuard 不算完成专用降噪 |
| 光流→NR 模型 | 独立实验，未作为产品导引路线启用 | 同 r4 Core/Kio/单层 50%，占位 MV、真实 AMD 导引、仅外部累积三组真实 Feature18 | 人工全局平移无复杂遮挡；不外推游戏、反射、HUD 或成本 |
| 独立残差 RR / NVOF | 独立研究，不能冒充已产品化能力 | 保留 SDK/语义与消费者缺口记录 | 无产品开关；不能仅补 MV 就满足 RR 材质/反射语义，不能用 AMD 结果代替 NVOF |

同源深度只用于保守深度差拒绝，没有重建上一视图空间位置，不能称完整几何遮挡判断。私有 SR 的原始 NGX 参数相位合同不依赖旧实验 `JitterMode`；旧实验选项继续默认关闭，保存选择不被重置。

## 本地证据索引与边界

- [r6 私有 Core WARP](%DLSS5_PRIVATE%/evidence/beta4-final-private-core-warp-r1/validation.json)、[显卡](%DLSS5_PRIVATE%/evidence/beta4-final-private-core-hardware-r1/validation.json)：各 17 个受控模式；控制模型输出不等于真实 NVIDIA 推理。[r6 集中验收控制器](%DLSS5_PRIVATE%/evidence/beta4-present-acceptance-controller/run-r6-cpp/result.json) 77 项通过。
- [r6 真实 NR/私有 SR 九场矩阵](%DLSS5_PRIVATE%/evidence/beta4-final-private-real-matrix-r1/validation.json)：真实 Feature18 1–5 层 50%、1 层 25% odd/100%、RR-only 与 RR+5 层均通过；RR 采用异环 `nvngx_dlssd.dll` 310.5，SHA256 `50eb21e654a1fd12403e000f5ac9c2488bf9149930c028e73196f3f1d47d10a9`。
- [r6 Present Adaptive](%DLSS5_PRIVATE%/evidence/beta4-final-present-adaptive-r1/validation.json)、[Low Frequency](%DLSS5_PRIVATE%/evidence/beta4-final-present-lowfrequency-r1/validation.json)、[Kio 关闭时域](%DLSS5_PRIVATE%/evidence/beta4-final-present-kio-r1/validation.json)、[20/30 关闭时域](%DLSS5_PRIVATE%/evidence/beta4-final-present-2030-r1/validation.json)：各 9 场，总计 36/36；原始 `+3` 来源夹具保留，两种累积每场均有 32 次录制/提交和 GPU 抽样历史采用。使用 r6 原生模块，不是诊断混合对象；人工输入不等于游戏验收或性能达标。
- 历史 [真实 NR/私有 SR 矩阵](%DLSS5_PRIVATE%/evidence/beta4-private-real-matrix-r1/validation.json)、[首层](%DLSS5_PRIVATE%/evidence/beta4-private-real-layer1-r1/validation.json)、[HD RR](%DLSS5_PRIVATE%/evidence/beta4-rr-full-hd-r1/validation.json) 保留原候选身份。旧小尺寸 RR 失败/挂起诊断继续保留在 `beta4-rr-*`。同一小尺寸夹具仅换异环实际 RR 310.5.0.0 后，RR-only + private Feature1 六帧、499 项通过（[回执](%DLSS5_PRIVATE%/evidence/beta4-rr-small-nte-runtime-r1/validation.json)）；2077 的 RR 310.1.0.0（`519c98e6d3b44d12671525f20e8fc46366a9fbf15aa5d4bc8446d6ba17699e22`）在该小尺寸挂起、HD 通过。这只说明受控库/尺寸组合差异，不解释 2077 原游戏崩溃，也不构成供应商最低尺寸结论。
- [时域 GPU 质量与 7 个变异负例](%DLSS5_PRIVATE%/evidence/beta4-temporal-quality-r2/validation.json)：2,285,603 checks；GPU 采用统计每 8×8 抽样，不是全像素计数。该次 debug layer 不可用。
- [r5 构建后八门预检](%DLSS5_PRIVATE%/evidence/beta4-postbuild-preflight-r1/validation.json)、[描述符来源补门](%DLSS5_PRIVATE%/evidence/beta4-postbuild-preflight-r1/descriptor)：CPU 2225、WARP 669；不替代 r6 canonical。
- [r6 三组光流模型实验](%DLSS5_PRIVATE%/evidence/beta4-final-model-flow-three-r1/validation.json)、[历史图像比较](%DLSS5_PRIVATE%/evidence/beta4-model-flow-three-r1/comparison.json)：最终三组均通过，source 8–16 平移对齐 MAE 维持占位 MV **0.00427990**、模型光流 **0.00327878**、仅外部累积 **0.00192451**。人工全局平移且三组都计算参考光流，不能据此推断游戏或开销。
- [r6 私有 SR 受控 GPU 成本](%DLSS5_PRIVATE%/evidence/beta4-final-private-cost-r1/validation.json)：三场各 32 帧通过，记录私有模块内部 GPU 时间、外部同步区间、导引和资源归零；不包含整帧及原生 SR/RR/NR 成本。[r6 4K 时域成本说明](%DLSS5_PRIVATE%/evidence/beta4-final-temporal-cost-4k-r1/COST_RESULT.md)：八场功能均通过，但 NR 基线和新增区间均呈多峰；无稳定产品成本或 ≤3% 结论，不把个别低尾段当常用开销。
- 两运行库基线与身份：[运行库合同](052_BETA4_RUNTIME_CONTRACT.json)、[#315 历史回执](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/315#issuecomment-5855101885)。RTX 5090 D 结果不等于 20/30 专卡实测。
- [Beta 3 原始接线与回执](PRIVATE_SR_CORE_052_BETA3.md)、[本地验收接续](../maintenance/LOCAL_ACCEPTANCE_FOLLOWUP_20260927.md) 保留原时间和候选身份。

## 验收门槛

[GPU 成本与资源记录](052_BETA4_GPU_COST.md)包含 4K 八场的完整 median/P95、分布限制，以及私有 SR 三场 32 帧的首次/稳定窗口与显存回落。

同 Core、同 DLL、同 NR 工作量，分别比较关闭、原生 MV、AMDOF、低频和私有 SR。检查静止/平移/物体运动/遮挡显露/切镜/透明反射/HUD/暗部/低比例/多层；“更稳但明显模糊或拖尾”不通过。

新增 GPU 成本须包括对应光流、导引转换、累积、拷贝及同步，记录绝对 ms、median/P95 和内存。r6 已完成受控私有 SR 三场和 O=3840×2160、N=50%、1/3 层时域八场，详见上列成本回执。4K 多峰明显，尚无稳定常用开销结论；受控 NR 链不是整游戏帧分母，私有 SR 局部 timestamp 也不含原生 SR/RR、NR、导引和整帧，**≤3% 目标未验收**。更快 DLL 的收益不能抵扣新增处理成本。

## 设置、集中验收和交付

- 新安装默认：`PrivateResidualSR=0`、`ResidualStabilizer=0`、`ResidualTemporalMode=0`、旧实验 `JitterMode=0`；两项省电默认关闭，AI 色彩范围 0.5。保留所有用户已保存选择，包括旧 jitter；不要求重置 INI。
- 集中验收五方案：当前设置、比例/色彩、时域路线、私有 SR、普通/低频。界面请求不是 GPU 采用；F8 需核对当前配置/实例、实际采用/拒绝/暖机及准入原因。过期配置的退休统计不能算新一组通过。
- 集中验收只临时改变插件设置，完成与取消均恢复；游戏 RR/HDR/FG 由维护者手动调整。先 FG 关闭，先异环、2077 最后；部署不启动游戏，实测继续由维护者安排。
- 图片采集与性能计时分段；同一场景手动留图比较噪点、文字、边缘和有效修改量，避免把截图开销计入性能窗口。
- Manager 仅完成[合同交接](https://github.com/smartLanny/DLSS5-Manager-ZJZ/issues/2#issuecomment-5855337815)：Core OTA 与运行库升级分离、保留设置、字节回退；未修改其默认 pin、安装配方或活动分支，实际采纳待对应维护者完成。

## 最终候选与回退

[完整交付身份](052_BETA4_DELIVERY.json)与[双语手测](052_BETA4_MANUAL_CHECKLIST.md)是本批入口。独立包位于 `%DLSS5_PRIVATE%/deliveries/052-beta4-acceptance-20260927`；Core ZIP 与两组运行库分开，不能将 Core-only 更新记成运行库更新。

- [r6 canonical validation](%DLSS5_PRIVATE%/deliveries/052-beta4-unified-85b59bdc-20260927-r6/validation.json) exit 0；[候选 JSON](%DLSS5_PRIVATE%/deliveries/052-beta4-unified-85b59bdc-20260927-r6/0.5.2-BETA.4-CANDIDATE.json) SHA256 `9342fe73e44bb3363df065cc346fa613b112aa36112a7a6759adcfd19c845710`；[source-manifest.json](%DLSS5_PRIVATE%/deliveries/052-beta4-unified-85b59bdc-20260927-r6/source/source-manifest.json) SHA256 `ee983859d3e529d4aed93bba6ff855af0d7b99f8a8c3c166ca09a1f33691c1de`。
- 中文 Core `nr-before-sr.0.5.2-beta.4.zh-CN.addon64` SHA256 `061561fe76e758bded01ff85080e42768151a15017ce3442da04b5f57ec2db9a`；[中文 ZIP](%DLSS5_PRIVATE%/deliveries/052-beta4-unified-85b59bdc-20260927-r6/DLSS5-Core-0.5.2-beta.4-zh-CN-test.zip) SHA256 `8f2da829298346ca44144351db5f806d98beaf7c527cd68edab4c1b9d847ebfa`。
- 英文 Core `nr-before-sr.0.5.2-beta.4.en.addon64` SHA256 `30ec1d00c4f4e4f916524db606fa0afc87016d91536e85e6b4700d73638abd77`；[英文 ZIP](%DLSS5_PRIVATE%/deliveries/052-beta4-unified-85b59bdc-20260927-r6/DLSS5-Core-0.5.2-beta.4-en-test.zip) SHA256 `4d725724daf6d73b9719bb32d85ec07df5e1cd071f9264188a8fd5cb877e5d2d`。
- 正式 Present 和受控 GPU 成本回执见上；[独立交付身份](%DLSS5_PRIVATE%/deliveries/052-beta4-acceptance-20260927/DELIVERY.json) 已建立。**稳定常用成本、整帧比例、游戏画面与稳定性仍待验；部署及回退已核对**；canonical 自身 `nvidia_feature18_tested=false` 指其构建链范围，之后独立真实 NGX 矩阵另有上列回执。
- 异环已由 6sol 部署中文 Core 与 Kio 40/50 NR 库；[安装回执](%DLSS5_PRIVATE%/evidence/052-beta4-nte-install-20260927/installation-receipt.json)确认唯一 Core、配置原字节和原生 SR/RR 不变，未启动游戏。[回退包](%DLSS5_PRIVATE%/evidence/052-beta4-nte-install-20260927/rollback.zip)及[使用说明](%DLSS5_PRIVATE%/evidence/052-beta4-nte-install-20260927/README-rollback.md)位于搜索目录外，回退脚本 VerifyOnly 已通过。

2077 的 unified10、0.5-d13、0.4.7 本机历史故障保留；未发现混版证据，根因未证明。不把这批人工宿主成功写成游戏问题已解决。

## 保留的研究依据

以下为本批既有研究入口，不能替代产品回执：[NVIDIA RR 接入要求](https://raw.githubusercontent.com/NVIDIA-RTX/Streamline/main/docs/ProgrammingGuideDLSS_RR.md)、[Magpie 0.6.8 实验版](https://github.com/SAOG0721/Magpie/releases/tag/v0.6.8-experimental.1)、[Opti v0.8.0 上游核查](https://raw.githubusercontent.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/v0.8.0/docs/NR-UPSTREAM-REVIEW.md)。本项目使用自有实现，不复制 Magpie 源码；研究方案的存在不等于已证明本候选的画质或兼容性。
