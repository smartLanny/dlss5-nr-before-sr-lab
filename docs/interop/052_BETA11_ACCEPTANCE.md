# Beta 11：可理解的实验状态、显存记录与私有 SR 预算

本记录对应产品源码 `1329c116f0108621d342214c466f6f41c1a305bd`。完整双语 canonical、受控 Core/WARP 18 模式、双语本地包及真实 Manager 解析、独立字节核查已通过；真实 NVIDIA NGX、硬件预检和安装尚未执行；游戏画质、显存收益与整帧成本仍待验证。异环游戏尚在运行，已装 Core 仍是 Beta 10。本次仅修改文档，**不会改变已构建 Beta 11 的产品源码身份或包字节**。


## 最终构建与交付回执（2026-09-29）

- 第六次 canonical：[`shell-receipt.json`](%DLSS5_PRIVATE%/evidence/beta11-canonical-1329c116-r1/shell-receipt.json) 状态 passed、实际 shell exit 0、源码/HEAD 均为 `1329c116f0108621d342214c466f6f41c1a305bd`、前后 clean；[候选](%DLSS5_PRIVATE%/deliveries/052-beta11-unified-1329c116-20260929-r1/0.5.2-BETA.11-CANDIDATE.json) 与[构建验证](%DLSS5_PRIVATE%/deliveries/052-beta11-unified-1329c116-20260929-r1/validation.json)相符。生成源码清单 SHA-256 `adf7b1b1416a3d722be2ba6cc760347491d936a805c0fa48b4ae60e09dcb3ce6`。
- [实际 Core/WARP 回执](%DLSS5_PRIVATE%/evidence/beta11-private-core-1329c116-r1/validation.json)：受控模型输出和合成输入的 18/18 模式及编译均 exit 0，包括 `budget-preflight`。[该模式日志](%DLSS5_PRIVATE%/evidence/beta11-private-core-1329c116-r1/warp-budget-preflight.log)记录 4K 需求 1,095,303,168 字节、可用上限 805,306,368 字节，**首次创建前拒绝，planes=0、privateCreate=0**；没有真实 NGX 推理或游戏证明。
- [双语包装与真实 Manager 解析](%DLSS5_PRIVATE%/evidence/beta11-ota-1329c116-r1/validation.json) passed；Manager UI 未测，未发布 OTA。[独立核查](%DLSS5_PRIVATE%/evidence/beta11-independent-1329c116-r1/independent-validation.json)重算生成源码清单逐文件、Core/ZIP 同字节、双语实际 Manager 解析日志及 18 模式，状态 `passed-artifacts`。中文 Core/ZIP SHA-256：`5494a29f7e8737d8f0424d222603aed596b8e2b90d56b42106dc6131139d98ef` / `c0e1f44b874fc9b03c9a44b02cb20cbd652c90e3f42c8d95af7532b10e82b30b`；英文：`889cb72c1030fd1583f90b463a5b3bdece2048527974452446ff492bc35d4e1c` / `c6a2f051ac8c7ba747bff6041797874438faef0cbed2a16e29aa387672e55911`。
- [等待游戏退出回执](%DLSS5_PRIVATE%/evidence/beta11-prep-20260929/pending-game-exit.json)记录 HTGame PID 26880 正在运行，硬件验证与安装均 `not-run/game-running`；中文 Beta 10 Core 哈希仍为 `e77e99c038a37b1746f34ace7173fc70af7df9812a48a6a610b73db12c7a56fe`。真实 NGX、Beta 11 游戏画质/VRAM/整帧成本和 Esc 修复均未证明。先核实退出并完成硬件与安装，再按[Beta 11 测试单](052_BETA11_MANUAL_CHECKLIST.md)实测。

前五次失败 canonical 的原回执按源码身份保留，不改写为通过：`32f08a68`、`cd636d21`、`cf1e39e6`、`66c08474`、`a8927fff` 分别位于 `%DLSS5_PRIVATE%/evidence/beta11-canonical-<SHA前8位>-r1/`；最终通过的是独立的 `beta11-canonical-1329c116-r1`。

Canonical acceptance 宿主确实在 `F:/documents/DLSS5-Feedback/20260929-074954-61456-904134437/` 与 `...904134468/` 导出 `memory-samples.csv` 和 `screenshots.json`：CSV 有 **18 列表头、0 数据行**；两份 JSON 各九组，`memorySampleRows=0`、无溢出，各组成功/失败/测量采样数均为 0。这只验证导出结构，既不是游戏日志，也不能作为显存实测或节省量证据。

## 证据和修复范围

2026-09-29 14:01:52 Beta 10 F8 不是 Esc 动画同步窗口。该窗口原生 RR 和普通后置 NR 均持续执行；私有 SR 有请求但捕获、录制、合成、提交为零，防闪关闭。不能据此评价私有 SR 画质或 Esc 根因。

同设备实际 D3D12 分配信息查询复现：4K、100% 私有 SR 的导引纹理 300,810,240 字节，后端纹理 802,160,640 字节，合计 1,102,970,880 字节，超过既有 805,306,368 字节上限。导引字节精确匹配 F8 的部分分配峰值。旧代码先分导引再尝试后端，失败后释放，下帧再次尝试；新增完整预检及按身份绑定的有界重试，不抬高上限、不放宽 GPU 生命周期。50%/25% 同设备纹理查询分别为 439,418,880／266,993,664 字节，只证明纹理预算适配，不含 opaque NGX、游戏其它资源或整帧性能。

UI 简化实际统一页和实验子页，帮助文字移入悬停/折叠；保留控件 ID、保存设置与默认值。请求、已提交、实际采用及画质有效分别表述。“暗部彩点保护”是原近黑色度算法，不是新换的全面降噪。

九组继续原时序，显存沿用 worker 低频 DXGI 查询，新增逐样本组/配置/阶段/时刻和成功失败计数、覆盖时间；旧测量字段保持原定义。不存在的样本写未知，采样最大值不等于瞬时峰。插件已知资源与进程总量不可混称为 NR 模型总显存。

## 画质与历史边界

Beta 7 曾修正私有 SR 编码和受控重建；Beta 9/10 未替换普通 NR 模型或交付新的已达标全面去噪核。Beta 8 新四点候选噪声 RMS 降约 55%，但细线修改积分仅普通恢复 58.85%，未过 98% 门槛，停止且不装产品。现有均衡/精细空间恢复、暗部彩点保护和防闪模式各有明确输入条件；弱效果反馈仍有效。

GitHub #317 已实时核对：9 月 28 日建立 Beta 7 异环 Esc/UI 专单，#318 只修其中已证明的 RR 首层深度绑定拒绝，不能说明 Esc A、UI/模型 B 已修。D13 实际源码为 7abd23569d0a64691caa8cd9adcff3596d6a04a9，本来已有资源变化后退休/重建机制；用户记忆中的 D13 无症状仍是待验证成功端点，首个引入版本未知。不用燕云 #246 代替异环证据。

原生 RR→NR 已交付；NR→私有 Feature13 RR 未交付。后置 RR 原生 MV 继续拒绝；不叠加私有 SR 与外部历史。Esc、切镜、Present、地图省电、多层暗部、真实游戏显存差值与整帧成本 ≤3% 仍由维护者实测。

本地证据目录：%DLSS5_PRIVATE%/evidence/beta10-feedback-20260929-afternoon-r1。含原始日志快照、功能核对、算法/显存表、Esc 时间线、GitHub 只读回执及分配信息探针。原始玩家材料不上传公开仓库。测试步骤见 [维护者测试单](052_BETA11_MANUAL_CHECKLIST.md)。

Esc 时间标记只读前台 ReShade runtime 的缓存输入，在既有快捷键锁下限量记录（按下、约 500／1500 ms）。没有读取任意文本输入、控制按键、开启窗口、切换游戏模式或等待 GPU；通过现有状态变化日志做关联。它是观测补齐，不是 Esc 修复。
