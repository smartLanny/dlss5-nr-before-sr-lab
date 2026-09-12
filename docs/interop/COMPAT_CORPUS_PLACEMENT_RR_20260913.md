# 兼容语料：placement / RR / lifecycle / MV（家族 B）

日期：2026-09-13。起点：`codex/dx12-compat-multipass-20260912@3c13f69`。
范围：公开 OptiScaler / RenoDX / Magpie Experimental **症状 → 契约 → 通用原语**。
不是 exe 白名单，不是强制中间件，不 vendor GPL 树。

Issues REST 对本集成仍是 403（与 [PR #260](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/260) 相同）。
本表按 #164 策略、D15/D16 文档、已开 PR，以及公开上游交叉核对。
游戏名只是复现入口。

## 先看这几条

- **不要**把 Opti / Reno / AIO 接到 Native DLSS 当强制中间件。它们是冲突检测 + 语料。
- **#190 Auto 梯子必须自研。** Reno 的 HookPoint Auto/Upscaled/Hudless/Present 只提供阶段分类，没有我们的会话保持、三起点顺延、同源一次、recorded 排除。
- **#254 ViewToClip / current-projection ≠ Magpie 零深度。** Opti 自己也说 RR 输入比 SR 多，缺资源时 NGX 成功仍可能是黑图。缺矩阵就旁路原生 RR，不要编近远平面。
- **#257 已有实现刀**：[PR #257](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/257)。不要平行再开一套 Present。32/256 是重试策略，不是 GPU 完成。
- **Top 3 Partial（可借契约，不是抄代码）：**
  1. Present/尺寸滞回 — Opti NR [#29](https://github.com/Dagherbou/OptiScaler_DLSSNR/issues/29)
  2. 创建帧信封 + 屏障按到达状态 — Opti NR [#16](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/16)
  3. MV 分辨率/子矩形/独立缩放 — Opti NR [#42](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/42)
- **#234 只记笔记。** Opti Reflex / Dx11on12 fence 是帧节奏，不是 RR→NR 退休算法。已有 [PR #236](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/236)。
- **#227 没有可用极性文档。** Opti `JitterCancellation` 是 MV 标志，不是 Y 轴符号。默认保持关闭。

## #164 评论表（可直接粘贴）

| Our issue | Upstream hit? | Contract | Likely help? | Next action |
| --- | --- | --- | --- | --- |
| #190 Present↔Evaluate auto routing | Partial：Reno HookPoint 0–4；Opti Present 是 FG 不是 NR 梯子 | 三起点 + 会话保持 + 同源一次 + 前 FG 消费 | **No**（梯子本身） | 自研 Auto；只借用阶段名，不抄 Reno Auto |
| #257 Present bounded re-probe | Partial：Opti NR #29 尺寸滞回；PR 已存在 | Adopted Present 在连续原生机会无采用后有界重探；exact fence | **Yes** | 审 [PR #257](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/257)；32/256 待测，不伪造 source |
| #254 鸣潮 RR current-projection | No：Opti [#652](https://github.com/optiscaler/OptiScaler/issues/652)/[#1132](https://github.com/optiscaler/OptiScaler/issues/1132) 明确 RR 不能猜输入；Magpie 零深度禁止 | 本次 Evaluate 的 `ViewToClipMatrix`；无效则 rr-safe-bypass | **No** | 保持 current-projection；缺矩阵不 bypass 成零深度 |
| #226 safe-bypass → post-NR | Partial：Reno 可换 hook；我们已有 Evaluate 前置→后置 | 仅未录制可顺延；recorded / FG 已消费 / 未知源不重试 | **Partial** | 做 recorded 排除 + 后置独立准入；不要 Reno Auto |
| #225 2077 queue-affinity flicker | Partial：Opti NR #16 屏障/信封；wiki Resource Barriers；jitter 关仍闪 | 同队列证明；到达状态而非假定状态；插入后恢复 | **Partial** | 通用 barrier-from-fact + restore envelope；不关 queue 保护 |
| #205 DS1 command-state | Partial：Opti NR #16 Create/Evaluate 分列表 + 创建帧信封 | 无 Reset 的首轮在覆盖证明下可观察；不伪造 Reset | **Partial** | 保留首轮录制合同；借信封，不借 Reset 伪造 |
| #223 SOTTR observer-uncertain | Weak：Reno [#651](https://github.com/clshortfuse/renodx/issues/651) 等 DLSS 等到死 | 未知句柄 ≠ Feature1；observer epoch；不按 DLL 名分类 | **No** | 保持 unknown-handle；不升级成 Present/后置 |
| #246 燕云 brief native flash | Partial：同 #257 Adopt-then-fail；#29 跳过未站稳尺寸 | 源/资源变化不得闪回原生后永久钉死 | **Partial** | 跟 #257 同一重探原语；对照源身份，不另开游戏枝 |
| #249 DL2 queue-affinity | Partial：契约同 #225；Opti DL2 单是崩溃/无 DLSS，不是亲和性 | 同队列准入 / 异队列拒绝 | **Partial** | 与 #225 共用原语；Opti #490/#501 不作白名单 |
| #234 RR→NR fence wait | Note：Opti Reflex/`ForceReflex`、Dx11on12 Fence+Event | 精确 fence；不 `Sleep(1)`；不加逐帧 CPU wait | **Note only** | 已有 PR #236；Reflex 只作节奏笔记 |
| #227 jitter pass default | No polarity：Opti 只覆盖 `MVJittered` 标志 | 实验 jitter 默认关；符号未证明 | **No** | 不因 Opti 翻转 Y；#225 已证明关 jitter ≠ 不闪 |

Likely help：**Yes** = 可直接变成我们的通用合同；**Partial** = 症状同类，原语可借，实现必须自有；**No** = 上游帮不上或会把我们带偏。

## Top 3：具体hints + 链接

### 1) #257 / #246 — Present 采用滞回（Yes）

**症状：** 加载/过场一次 Adopt Present 后，原生 DLSS 回来也不回去；或尺寸/源来回跳时闪回原生。

**上游：** [OptiScaler_DLSSNR #29](https://github.com/Dagherbou/OptiScaler_DLSSNR/issues/29)
MSFS 双 Feature 交替尺寸 → 每帧重建 → VRAM 爆炸。提案：新尺寸连续 K 次才信；未站稳的另一尺寸跳过。

**我们的原语（不要抄 GPL）：**
- 连续 **原生重建源机会** 无采用，才挂起重探（PR #257：Adopted=32，暖机=256）。
- 录制 ≠ 采用续期。重复/过期源不推进计数。
- 手选 Present 不重探。健康连续采用不周期性打断。
- 回到 Before/After 前：Present fence **和** native NR 退休谓词都允许。
- 异环 `submitted=1 / source=content=exactWrite=0` **不是**本刀；禁止把最新 SR 贴到任意 backbuffer。

**下一步：** 审 PR #257，不要再开 Present PR。

### 2) #225 / #249 / #205 — 到达状态 + 创建帧信封（Partial）

**症状：** 2077/DL2 队列亲和闪；DS1 首轮无 Reset；NR 插入后游戏从错误绑定恢复。

**上游：**
- [OptiScaler_DLSSNR #16](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/16)：Create 与 Evaluate 不共享列表；创建帧也要进状态信封；Depth/MV/Exposure 屏障读 **到达** 键，默认未设则 no-op。
- [Opti wiki · Resource Barriers](https://github.com/optiscaler/OptiScaler/wiki/Known-Issues#resource-barriers)：引擎常报错状态；假定 `NON_PIXEL_SHADER_RESOURCE` 会 debug-layer / 丢设备。
- [Opti wiki · Display Resolution MVs](https://github.com/optiscaler/OptiScaler/wiki/Known-Issues#display-resolution-motion-vectors)：`DisplayResolution` 标错 → 运动糊。

**我们的原语：**
- 屏障从观察状态出发，不从 NGX「应当」状态出发。
- 私有 NR 绑定后恢复游戏 root/heaps（已有信封则扩到 **创建帧**，不要只包 Evaluate）。
- 未知队列：证明同队列或旁路；禁止猜第二队列。
- DS1：覆盖证明下观察无 Reset 首轮；失败 Dummy PSO / 未知状态仍旁路。

**不要：** 按 2077/DL2/DS1 exe 分支；不要为闪烁关掉 queue-affinity。

### 3) MV 元数据合同 — 闪烁家族的共享输入（Partial）

**症状：** SR（非 DLAA）阴影闪、运动糊；后置把已 jitter 的 MV 再取消一次。

**上游：** [OptiScaler_DLSSNR #42](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/42)
NR 曾假定 MV 总是 guide 分辨率且从 (0,0) 开始。DLAA 碰巧能过；SR 下尺寸/子矩形/缩放错 → 阴影闪。修复：尊重 `MVLowRes`、转发游戏 subrect、独立 XY 缩放、夹到资源边界。

**我们的原语：**
- MV 尺寸跟 `MVLowRes` / 实际资源，不跟 color。
- 子矩形与 depth 一样做边界夹取。
- `MVecScaleX/Y` 独立，不合成单一 scale。
- 后置：游戏已标 `MVJittered` 则不要再当 raw jitter（`nr_face_runtime.inl` 已有，需升到主路径并加负控）。

**不要：** 把 Opti `JitterCancellation=true/false` 当成 Y 极性；那是 init **标志**。

## 必须留在我们这边

| 项 | 为什么上游帮不上 |
| --- | --- |
| **#190 Auto 梯子** | Reno Auto 是 hook 选择，不是「菜单/加载/FG/无 DLSS」会话状态机。我们要：前置→后置→Present、会话钉住、点按钮才重试、同源一次、FG 前消费。 |
| **#254 current-projection** | Magpie NR [选零深度](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp)。Opti [#652](https://github.com/optiscaler/OptiScaler/issues/652)：「RR 输入比 upscaler 多，Opti 没有这些信息」。[#1132](https://github.com/optiscaler/OptiScaler/issues/1132)：NGX success ≠ 有效像素。缺 `ViewToClip` 就 `current-projection` 旁路。 |
| **recorded / poison 收据** | SF9.8.5 实验：Recorded 不能降成 RejectBeforeWork；矛盾使用毒化该设备代次。Reno 无此合同。 |
| **未知句柄 / #223** | 不按文件名把 Feature18 标成私有。Reno #651 的「Require DLSS 等到死」是反面教材。 |
| **source 血统** | PresentCount / 纹理指针 / 重复 jitter ≠ 跨入口身份。PR #260：不要把最新 SR 贴到任意 backbuffer。 |
| **第二套 NR Core** | NIGos 只是桥；外部时域 → SourceFrame → 自己的 Core。 |

## 其余 issue 短注

**#226** — Evaluate 前置→后置已在 dev12。剩下：旁路后的 **兼容/Present** 顺延，且不得在 recorded/FG-consumed 上重跑。Reno 换 hook ≠ 我们的收据合同。

**#223** — 生命周期切片已修 nested Create 丢弃 / Feature18 排除；SOTTR 根因未证。下一步：未知句柄的 Create 血统，不是放宽 guard。

**#234** — [Opti `ForceReflex` / `FramerateLimit`](https://github.com/optiscaler/OptiScaler/blob/ac0a647ce7ce78ceebcedb93e14e53d1c96b12c3/OptiScaler.ini) 绑 Present/FG；Dx11on12 用 Fence / Fence+Flush / Fence+Event / Query。教训：GPU fence 优于 CPU event，event 优于 sleep。我们的 RR→NR 退休已有 exact-fence + [PR #236](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/236) 早事件接续。不引入 Reflex 中间件，不加逐帧 wait。

**#227** — 公开材料没有 jitter **极性**（Y 翻转 / 归一化空间）。Opti 只覆盖 `NVSDK_NGX_DLSS_Feature_Flags_MVJittered`。Reno 公开源仍无对应 NR addon。我们默认关；#225 在 jitter 关闭后仍闪，所以默认迁移不是闪烁修复。

**Magpie（次要，只谈 residual/placement）：** 缩小输入、只恢复模型修改量、导引要同帧同网格。**禁止**把它的零深度带到 RR。placement 启示：残差落在颜色，深度必须是当前投影证明过的引擎深度。

## 策略对齐（#164 / PR #260）

已一致：

1. 先审 PR #257。
2. #190 源/Present 血统另刀（`submitted` 无 source 不是 #257）。
3. 不把 Opti/Reno 当强制中间件。
4. 不为 #254 在缺 ViewToClip 时 bypass RR 深度。
5. 不 vendor 大树、不改 LICENSE。

本文件只增加语料表与 Top 3 hints，不改生产代码。

## 证据边界

- 公开：OptiScaler 主仓 issues/wiki/ini、OptiScaler_DLSSNR PRs、clshortfuse/renodx issues、已核验的 Reno/SF 对照文档、SAOG Magpie 固定提交说明。
- 未执行任何上游二进制，未复制 GPL 源。
- 本仓 Issues 正文因 REST 403 未直接读取；标题与范围来自 D15 验收表（2026-09-12 只读核对）及 PR #257/#260/#236。
- 无 NVIDIA / 游戏 / 托管 CI 结论。本地未跑新门禁（仅文档）。

## 建议 #164 评论正文

```text
家族 B 语料（placement / RR / lifecycle / MV）。公开 Opti/Reno/Magpie → 通用契约，不是 exe 白名单，不是强制中间件。完整表：docs/interop/COMPAT_CORPUS_PLACEMENT_RR_20260913.md

| Our issue | Upstream hit? | Contract | Likely help? | Next action |
| #190 Auto 梯子 | Reno HookPoint 分类 only | 会话+三起点+同源一次 | No | 自研；勿抄 Reno Auto |
| #257 Present 重探 | Opti NR #29 滞回；PR 已在 | 有界重探 + exact fence | Yes | 审 PR #257 |
| #254 鸣潮 current-projection | Opti #652/#1132 拒猜 RR | 本次 ViewToClip | No | ≠ Magpie 零深度 |
| #226 bypass→后置 | Reno 换 hook ≠ 收据 | 仅未录制可顺延 | Partial | recorded/FG 排除 |
| #225 2077 队列闪 | Opti NR #16 屏障/信封 | 到达状态+同队列 | Partial | 通用 envelope |
| #205 DS1 command-state | #16 Create≠Evaluate 列表 | 无 Reset 首轮可观察 | Partial | 不伪造 Reset |
| #223 SOTTR 未知句柄 | Reno #651 反面：等到死 | 未知 ≠ Feature1 | No | 保持 guard |
| #246 燕云闪回原生 | 同 #257 / #29 | 采用失效后重探 | Partial | 跟 #257 |
| #249 DL2 队列 | 同 #225；Opti DL2 是崩溃单 | 同队列合同 | Partial | 共用原语 |
| #234 RR fence | Opti Reflex/Dx11on12 笔记 | exact fence | Note only | 已有 PR #236 |
| #227 jitter 默认 | 无极性文档 | 默认关 | No | 勿翻 Y |

Top 3：#257←#29；#225/#249/#205←#16；MV←#42。
必须自研：#190 Auto、#254 ViewToClip、recorded/poison、未知句柄、source 血统。
```
