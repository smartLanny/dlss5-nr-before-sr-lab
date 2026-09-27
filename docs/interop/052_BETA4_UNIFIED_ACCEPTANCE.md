# 0.5.2 Beta 4 统一候选工作与验收

状态：开发中，尚未冻结、尚未交付。版本号保留给 #313 的本批统一候选。
本页是本批需求、证据和缺口的索引；旧 Beta 1/2/3 包及回执不被重命名。

## 范围与责任

- 分支 feat/309-post-private-sr，草稿 #313；已纳入 main b2990b12、#316 3e597945、#308 13d1cca7、Beta 2 e14dc28b。
- 本批先修运动与历史，再根据剩余噪点决定是否增加空间处理。
- 游戏全部由维护者验收。6sol 只核对、部署与准备回退；先异环，2077 最后。
- 游戏通过前不合 main、不发 OTA、不修改 Manager 默认版本。
- 用户已有新 DLL 性能观察：20/30 提升较大，40/50 有细微提升。开发侧重点验证兼容，不把该收益用于抵扣新增处理成本。

## 功能矩阵

“接线”与“验证”分别记录；本表开发状态不构成已交付能力。

| 能力 | 当前消费者/入口 | 本批工作 | 验证层级/缺口 | 明确拒绝 |
|---|---|---|---|---|
| 原生 MV | 原生 SR 后置外部累积 | 校验来源、单位、相位及历史约束 | 新 Core 软件与游戏待测 | 无效单位、未知相位/来源 |
| AMDOF | 原生 SR 前/后与 RR 后的外部累积 | 增加真实历史采用/拒绝，普通/低频互斥 | 新 Core 软件与游戏待测 | 未证明域/源帧、错误队列 |
| Present AMDOF | Present 最终 B/Z | 真实内容谱系、独立录制/提交/退休 | 本批接线中；需实际 Core | D3D11、HDR、FG、多交换链、仅近期原生回执 |
| 光流→NR 模型 | 独立受控实验 | 占位 MV / 光流导引 / 仅外部累积对照 | 人工平移单层正例；第三组/遮挡/成本与游戏尚待，未进入产品 | 不把外部累积当作模型已接光流 |
| 私有 SR | SR/RR 原生后置全链 B/Z | 原生 jitter、Flags75、同域 FP16 HDR、RR-only 域 | 新 Core 与真实 Feature18+1 待测 | Present、FG、多 GPU、外部累积同时请求、未知编码/相位 |
| 剩余孤立噪点 | NR 修改量 | 时域修正后比较人工正反例与真实暗部素材 | 不提前增加 ResidualDenoise 菜单 | 未过质量/成本门槛不能称达标 |
| 前置旁路 | 原生 SR 输入前的 NR | 保留独立路线和源帧证据 | 不等同后置私有 SR | 不伪装私有重建 |
| 跨原生 RR | 游戏原生 RR 的修改量传输研究 | 保留独立结论 | 不宣称独立 RR 已实现 | 缺深度/遮挡与材质语义 |
| 独立 residual RR | 研究 | 检查真实材质、反射及残差语义 | 未提供产品开关 | 不能只补 MV 就准入 |
| NVOF | 独立来源研究 | 记录 SDK/硬件/消费者与缺口 | 未接产品 | 不能用 AMDOF 结果代替 NVOF 证据 |

## 验证门槛

- 同 Core、同 DLL、同 NR 工作量，分别对比关闭、原生 MV、AMDOF、低频、私有 SR。
- 新增成本包含光流、转换、累积、拷贝和同步；记录绝对毫秒、中位数、P95、显存及独立开启成本。
- 常用方案新增 GPU 帧时间目标 ≤3%。明显有效但超预算可保留手动实验；没有完整帧 GPU 时间分母时不得称通过。
- 同时检查噪点、闪烁、拖尾、细节响应与有效增强量。更稳但明显模糊/拖尾不通过。
- 场景：静止/平移/物体运动/遮挡显露/切镜/反射透明物/HUD/暗部/低比例/多层。
- 包括实际生成 Core、真实 Feature18 一至五层、Feature1 和无 SR 父实例的 RR-only 宿主；人工场景不是游戏画质证明。
- 提交乱序/重复提交/Reset失败/丢弃/提前释放/槽满/预算不足均保留有效 Z 或按不可证明生命周期隔离。

## 设置与交付合同

PrivateResidualSR=0；ResidualStabilizer=0；ResidualTemporalMode=0（普通，1=低频）。
旧 JitterMode=0 默认保持，已保存选择不迁移重置。两项省电默认关闭，AI 色彩范围 0.5。
本批没有预先承诺独立空间过滤开关。新增处理关闭不得增加对应光流、导引采集、私有模型或 GPU 工作。
集中验收只临时改变插件设置，完成/取消均恢复；游戏 RR/HDR/FG 由用户调整。预热须核对实际历史采用；采集和性能计时分段。

## 研究依据

- [NVIDIA RR 接入要求](https://raw.githubusercontent.com/NVIDIA-RTX/Streamline/main/docs/ProgrammingGuideDLSS_RR.md)：输入包含光追颜色、材质、深度和运动；反射还需要对应运动或命中距离等信息。因此任意带符号修改量是否满足 RR 语义仍需证明。
- [Magpie 0.6.8 实验版](https://github.com/SAOG0721/Magpie/releases/tag/v0.6.8-experimental.1)：互斥抗闪模式与低频时域可用于研究对照，但发行说明也记录拖影/细节延迟风险。本项目使用自有实现，不复制其代码。
- [Opti v0.8.0 集成限制](https://raw.githubusercontent.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/v0.8.0/docs/NR-UPSTREAM-REVIEW.md)：仍需验证时域涂抹及其他边界，不能将私有 SR 等同专用去噪器。

## 历史问题

2077 的 unified10、0.5-d13、0.4.7 均有本机故障记录，未发现混版证据，尚无已证明根因；不推断用户此前成功记录无效。游戏测试继续暂停到维护者集中验收。


## 已完成的独立验证（尚不等于冻结候选）

- 固定旧 Beta 3 Core 4f5ce993 + 两组 #315 运行库，当前 RTX 5090 D 上各九场真实 Feature18 检查通过：1–5 层 100%，2/3/5 层 50%，4 层 75%。GPU 回读校验每层完整传递与最终合成；F6 关闭后精确完成回收。
- 运行库完整哈希已现场重算；兼容结果已同步 [#315 进度](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/315#issuecomment-5855101885)。这不替代新 Beta 4 Core 回归。
- 省电/恢复五个 CPU 检查入口通过，包括真实生产适配器、并发源流隔离、恢复前重置历史、精确路线回探及 12 个恢复场景。新增有界 16 项暂停/恢复事件环；F8 读取不触发恢复、不改历史。
- 私有导引 CPU/WARP 已验证动态 jitter 与 floor-quarter 奇数尺寸；最终 Core 的同路径结果仍须追加。
- 现有 NVOF 仅合同枚举；实际 RecordedTemporal 构造明确拒绝该 backend。本机已配置工具目录未发现可核验 NVOF SDK。本批不以设备支持或枚举存在宣称实现完成。

## 现有恢复行为与游戏取证

省电入口只根据真实 pause/源帧信号决定；Esc 按键本身不是暂停证明。F8 组合查看：
energyGate.event（暂停/恢复/过期/关闭及对应历史代）、energyGate.blockedChecks、
uiActivity 的 carrier/NR/submitted 差分、guideDomain 拒绝原因及 resetObservation 源帧记录。
原生调用停止、主动省电和导引旁路分别保留；未有可复現游戏证据时不声称某游戏 Esc 问题已修复。

## 本批新增证据及 Manager 交接

- 新 Present 来源传输经过实际 WARP 1700 项检查；受控真实 Core 的 Present 外部累积仍待统一生成后验证。
- 光流作为 NR 模型导引独立实验：固定 Beta 3 Core 和 Kio 库，各 15 次真实 Feature18 Evaluate；单层、50%、非周期全图平移。两组实际 Reset 一致，source 7 后 AMD flow 可用，内区端点误差为零。source 8–16 平移对齐 MAE 为占位 MV 0.00427990、光流 0.00327878（约下降 23.4%）。此项没有证明遮挡/反射/HUD、拖尾或游戏降噪，未取得 ≤3% 成本门槛；不作为产品启用依据。
- 私有 SR 新增可选真实队列 timestamp，复用原 Flight 录制和 fence 退休。统计仅覆盖残差提取/Feature1/合成，分别报告首次创建、首次推理、排除前八次的滚动 median/P95；导引、原生 SR/RR、NR 和整帧分母不在此统计内。统一 Core 验证尚待。
- [Manager 现有组件任务交接](https://github.com/smartLanny/DLSS5-Manager-ZJZ/issues/2#issuecomment-5855337815) 已同步两组完整库身份、Core OTA/运行库升级的分离、配置保留与字节回退合同。没有改其活动开发分支、安装配方或默认 pin；Manager 实际采纳仍需对应维护工作完成。
