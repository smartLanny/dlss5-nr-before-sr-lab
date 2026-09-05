# R3 后集中开发与统一验收计划

快照日期：2026-09-06。本文是评估和测试计划，**不包含新的运行时修复，不代表任何游戏已通过本轮验收**。任务清单见同目录 `post-r3-assignments.csv`；一条任务可有多个独立 A/B 子组，结果必须分别填写。

## 固定基线，禁止混包

| 批次 | 用途 | 精确身份 / 起测门槛 |
|---|---|---|
| S | 已发布 R3 对照 | `beta0.4.3r3` / `d42a8eb23778bc602d0d638193872d41216c2203`，不是“全部游戏已验证稳定”的声明 |
| M | 0.4 维护源码 | `release/0.4@647ac292433bf00506eead16ccda1a30c2b3a153`；本次读取时与 `main@9737a16349a21efc2a3b73c7cef6ec6c78dc786f` 同树 |
| D | DX11 专用候选 | PR #79，head `0f714b32597e395d0684567ed9296dc1d2a1f35c`，目标 `release/0.4`；分派前维护者冻结实际核心+桥+提供者及整套 SHA，不能用普通 R3 OTA 代替 |
| Q | 0.5 画质候选 | PR #73，head `14957312ad42debd70bd5c8040fc433e05c502e9`，只进 `next/0.5`；独立候选，不反灌 0.4 |
| C | 下一轮修复/功能 | 尚待代码、CI 和二进制冻结；T31–T35 现在不能按“已实现功能”分派 |

R3 发布入口：https://github.com/smartLanny/dlss5-nr-before-sr-lab/releases/tag/beta0.4.3r3

PR 的源码 head、CI 测试 merge-ref 与实际 addon 哈希分别记录。仅看文件名或版本标题不能确认同一个包。所有测试仅限合法单机/离线或获准环境，不绕过反作弊/安全保护；只加载一份本插件及一个 NR 所有者。不同 FG 解锁实现不能并装。已有运行库按合法来源与 GPU 适配记录，不在本文分发。

## 最新事实改变了优先级

- **#53 异环：R3 部分恢复，不是整项解决。** RTX 5060 Ti / 616.64 样本的前置有持续 NR 成功；后置 `transfer-prepared` 增长而 Feature18/recorded/bound/submitted 为 0；启动有旁路交替。证据：https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/53#issuecomment-5554109850
- **#65 街霸6：R3 仍 unregistered-list。** 新的无 RenoDX 样本有约 8918 次 SR carrier 调用、registered=0；#67 的补登记没有覆盖这个现场。OptiScaler 截图来自另一台 RTX 3060 Ti，不是本插件验收，也不证明两个 NR 所有者可同时使用。证据：https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/65#issuecomment-5554342084
- **#68 NBA 2K27：R3 仍失败，但不是 null-PSO 同根因。** late-core / unknown-evaluate / module-observer-uncertain 必须独立定位；旧 R32_TYPELESS 曝光子症状另列。证据：https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/68#issuecomment-5554109692
- **#52 已补齐旧 0.4.2 完整归档并重开。** 不再按“未交附件”关闭；它也不是本机 R3 否定证据。证据：https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/52#issuecomment-5554676599
- **#38/#50/#64** 仍需要固定 R3 实机验收；**#58 只能用对应 DX11 桥候选验收**，不能笼统要求普通 R3 OTA。
- **#49 已有维护者归档的 dump/WER**。应先分析已有数据和当前构建差异，而非又索取同一份旧包；F8 不是 crash dump 的替代品。

## 建议开发顺序与可合并范围

### A. 0.4 兼容修复，分根因提交、统一验收

P0 为 #49/#18 的崩溃归因与最小修复；P1 为 #53 后置断点、#65 原生/代理列表身份及真实 owner 登记、#68 模块生命周期观察。

对 #53，源码检查已看到一个应优先排查的边界：`PrepareAfterSr` 先增加准备计数，而实际 `FinishAfterSr` 只在原生 SR 返回后新的 `NativeCommandStateScope` 可用时执行。**这说明 prepared 不等于已经调用 NR，但尚不能仅凭摘要认定唯一根因。** 应分别记录 SR 返回、后置状态拒绝、快照、参数绑定、Create、Evaluate 与提交，不可拿进入 SR 前的快照冒充 SR 返回后的状态。

源码依据：
https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/647ac292433bf00506eead16ccda1a30c2b3a153/src/nr_before_sr.cpp#L7750-L7990
https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/647ac292433bf00506eead16ccda1a30c2b3a153/src/nr_before_sr.cpp#L8915-L9048

同一引擎、同样“没有效果”或相同的粗略旁路类别，不足以合并根因。#36 不自动归入 #65，#63 外部热补丁不自动归入 #68。保留 RR、原 NGX 参数、真实队列/fence、subrect、资源生命周期及未知 HDR 保护。

### B. 适合集中落地的体验/小功能

1. UI/报告闭环：明确请求与实际 R/N/O、当前模式和失败阶段、最近有效提交及时间；保持配置 generation、F8/F7 锁与旧配置迁移。显示平滑不能掩盖真实旁路或把一次成功显示成持续成功。
2. #2 保守的 FP16 typeless 颜色视图兼容：单独验证设备支持、SRV/UAV flags、尺寸与原输出保持。**不与 NBA 的 R32 曝光合并成“所有 typeless 都当 float”**。
3. #9 分阶段 GPU 计时：先有界异步 query、真实队列频率和有效提交样本，再考虑自动工作比例；先不同时自动改 R/N/强度。
4. FG/MFG：先做分实现的共存矩阵和真实能力/状态显示；倍率控制须有可验证 ABI、线程/生命周期与生效确认。#63 的入口冲突尚不能用覆盖第三方热补丁解决。

### C. #82 留在 0.4，但分两道门槛

前置的百分比 UI 可复用已有工作比例；后置低分 NR 是执行路径变化，不只是解除滑条禁用。建议先保留 100% 完整后置参考，完成固定 75% 候选的原图保持、identity、guide/采样相位、HDR 与成本验证，再扩展连续比例。

提议的明确语义（实现前仍需冻结）：前置显示 N/R，后置显示 Npost/O；**0% 表示关闭 NR，不创建零尺寸纹理**；其余请求受模型/格式/对齐下限限制，显示请求值与实际尺寸，不能“滑到 1% 实际仍跑 50%”却不提示。旧 WorkMode/CustomWorkScale 保留迁移与回退。R3 没有承诺这套新滑条语义。

低分后置保留完整 O，只从同帧匹配 proxy/neural 重建修改再合成；不以低分整图覆盖 O，不额外串一次 SR/NR。不要同时引入所有新的重采样核、时域算法和强度数学，让测试无法归因。

相关请求：https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/82

### D. 已有候选不重复开发

#79 的 DX11/真实 R 查询和 #73 的高强度保护都有实际代码，但在不同目标分支。先做各自验收，不整包混成一个版本。#71 的 5090 Laptop 首测为 dirty 构建且模型 Intensity/局部参数较高；不能仅按同名 Transfer 与 R3 比较后宣布颜色算法退化。

## 测试协议（CSV 中引用编号）

| 编号 | 执行要求与判定 |
|---|---|
| P00 | 核对构建/载荷SHA、游戏EXE/API、GPU显存、驱动、Windows、HDR、全部DLL/模组。备份配置与存档；单NR所有者；合法环境。 |
| P01 | 固定场景、R/O、模型、强度1与100%工作比例；NR关闭、Mode2、Mode1分组。每组独立scope和起止计数，成功增量+提交增量+F6可见对照+稳定性同时验收。 |
| P02 | 启动就观察，稳定后切窗10次、读档3次、可用时切分辨率/HDR各2次；基础通过后至少10000源帧或30分钟并写实际覆盖。记录显存起点/峰值/终点；崩溃或挂起立即停止。 |
| P03 | 分清unregistered、native-hooks、fresh-reset、graphics-state-used等；记录guard/route/meta覆盖与实际subrect。不得取整3839×2159或开启全局高风险守卫。 |
| P04 | 记录首次Hook失败与Create→Evaluate→Release、晚加载、别名/嵌套。NBA曝光与生命周期分别判断；未知/RR仍旁路。 |
| P05 | 固定模型/N/曝光，强度1再2；鸣潮HDR开/关×LightingLock 0/0.2/1。F8身份+配对F7+录屏；灰色HDR控件与黑边是两项。 |
| P06 | 无NR无FG、仅NR、仅FG、NR+FG四组。原生/Universal/MFGAda/ERSS分别冻结版本；移除提供者造成无SR载体则记N/A。记录基础帧与显示节奏，不以FPS单值验收MFG。 |
| P07 | 双语分别加载，100/150/200%DPI与双面板；滑条/F6/采集开始竞争、重启配置与identity提示。不能改其他插件全局布局/字体。 |
| P08 | 崩溃专项先分析已有dump/WER，专人最小复现，Enabled=0/1完整重启对照；出现异常停测。不能进游戏用离线采集，不重复索取已归档旧包。 |
| P09 | 成功与无NR游戏各采F8，连采/取消/重试/F7配对，核对SHA/session/路径/配置。核心≤8MiB，部分包可分析；无自动上传或临时配置持久化。 |
| P10 | #79整套核心+桥+提供者；BG3 DX11再第二提供者游戏。桥独立、NR关/开、有效N档、读档/重建/压力；验证同帧回填、shared fence和原生回退。 |
| P11 | #79真实R请求；FollowGame→请求→游戏重建→实际R/O→恢复。查询接受不等于R已变；F6只关NR不等于回退游戏R。 |
| P12 | #73与R3分组；统一所有模型参数，保护开/关及0/1/2/4/8强度。先基准再复现#71自定义配置；有效颜色强度0不算颜色跟随生效。 |
| P13 | 待#82实现。0/100/75/50及非整档、非法下限、旧配置迁移；后置先100%/75%与identity，合法尺寸及请求/实际N必须一致可解释。 |
| P14 | 待#9实现。计时关/开重复，关闭F7额外读回，记录真实query/队列频率、有效提交、median/p95；不能跨队列简单相加。 |
| P15 | 待#2实现。开发者WARP正负例先行，flags/view/越界拒绝；合法游戏随后，不预先宣称Helldivers2兼容。 |

## 结果填写与结案

每一子组至少填写：任务ID/子组、完整源码和addon SHA、真实GPU/显存、驱动/运行库/模组、模式与R/N/O、HDR/FG、scope/feature/config标识、NR/提交起止计数、画面差异、崩溃/显存/帧时间、F8/F7或转储证据位置。

只对相同计数scope相减；切模式/feature/config后重新建行。NR关闭组预期没有新的Feature18调用，已经在途资源可按原fence退休。画面、调用成功、已提交、已完成、显示节奏分别描述。负结果、受阻和N/A同样有价值，不能凑“通过率”。

所有任务初始未分派/未测。测试负责人在任务层汇总结论；代码PR、CI、WARP不能自动填写为游戏通过。Issue只有精确候选与原问题复测相符后才结案；#53前置成功不关闭后置，#52旧包不关闭当前版本调查，#63诊断完善不等于MFG已兼容。维护者已归档而未同步远端的日志先内部补充，不要求玩家再交同一旧包。

本计划不修改main、R3 tag或既有Release，不引入游戏私有路径、联系方式、截图或dump。