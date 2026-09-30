# 外部 NR 项目对照：显存、成本与可学习之处（2026-09-30）

起因：有人认为 wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass 的显存占用优于本项目。
本页只是**源码审查**：没有编译、运行、显存测量或游戏验证，不改变任何二进制身份。
关联 #298（显存）、#293（首开卡顿）、#277（整帧成本）、#317（Esc/UI）、#309（实验算法）、#315（运行库）。

## 0. 对照对象与身份

| 对象 | 身份 | 许可 | 用法 |
|---|---|---|---|
| 本项目 | main `12a9dd508c712ad9a75d34390172f791c4c19250`；`src/` 与 `scripts/patch-*.py` 与 Beta 13 产品源码 `cd8ba700` 逐字相同 | — | 下文 `:行号` 默认指 `src/nr_before_sr.cpp` |
| wilsjo2 OptiScaler NR | 发布 tag `v0.8.91` = `f45ccf3a761df91450959d325ac0166cba5364c9`（2026-09-23）。默认分支 main 停在 `1bd39091`，落后发布约 4 万行，**不能用 main 代表当前版本** | GPL-3.0 | 只学机制，不复制源码（同 `MAX_COMPAT_CONTINUATION.md`） |
| BeliyG3/optimizer-fps-dlss5 | HEAD `1bc2b76fa3dbdb5e41677286eb74907f63831235`（2026.9.2）；wilsjo2 的空间压缩取自其 `64902dd6` | MIT | 可附版权声明复用 |

已有引用：`052_BETA6_EVALUATION_AND_NEXT.md` 核对过 wilsjo2 main `1bd39091` 的 DeferredSr 与 RR 路由；
`052_BETA4_UNIFIED_ACCEPTANCE.md` 引用过其 v0.8.0 上游核查。本页基于更新的 v0.8.91，下文对方路径相对其仓库根，
`S/` = `OptiScaler/shaders/dlssnr/`，`N/` = `OptiScaler/dlssnr/`。

## 1. 结论

1. **“对方更省显存”目前没有可比测量。** 两边都没有同卡、同游戏、同设置、同运行库的数据。对方 NR 代码和测试里没有任何显存测量；
   它自己记录过 Cyberpunk 会话 34.03 GB、NR 时间从约 4 ms 升到 23 ms（`docs/NR-GPU-RETIREMENT.md:3`），
   v0.8.4 之前 12 次换模型残留 3,663 MiB（`docs/RELEASE-v0.8.4.md:11`，只有说明，`tests/` 中未找到复现）。
2. **大头是 NR 模型实例，两边一样：每层一个。** #298 评论（#319，Beta 7 `80dcc999`，5090D，2304×1296，只 Create 不 Evaluate）：
   单实例重建 746,618,880 B；5 个实例 3,510,403,072 B；释放 4 个留 1 个仍为 3,205,005,312 B，全部释放后才回到 53,051,392 B。
   对方同样每个 pass 一个独立 Feature（`S/DlssNr_Dx12_ModelState.h:14`），默认 1 层（`Config.h:351`），菜单上限 2，解锁后 10
   （`N/DlssNr_MenuControls.cpp:289`）。**同层数时这项没有差别；玩家常开 2–3 层才是主要差别。**
3. **源码上有四处结构差异，可能让同设置下我们占用更多**（按可能量级排序，均未测）：
   - **运行库不同。** 我们的 20/30 包带 plain FP16 运行库（309,671,536 B，是常规版约 165.8 MB 的 1.87 倍），40/50 包带 Kio v1.0.6；
     对方固定 RTX50 原版 310.8 `E16BCF15…`、20/30/40 用 ShortFuse `E67DEE20…`（`INSTALL-DLSSNR.md:17-24`）。
     权重和工作区不同，模型占用就会不同，与插件代码无关。
   - **非当前路线的模型仍驻留。** 我们前置（mode 2）和后置/RR（mode 1）的 context 分别匹配（`:7684-7686`），上限 `g_contexts[16]`（`:5609`）。
     只有显式改路线或减层才整体退休（`src/nr_residency_policy.h:19-35`）；自动路线回退不退休。SR 与 RR 句柄的 context
     要等游戏释放非活动句柄才退休（`scripts/patch-post-compat.py:41-49`）。对方每个 owner 只有一套模型，placement 或 SR↔RR 一变就全部退休
     （`S/DlssNr_Dx12_Models.cpp:17-48`）。叠加 #319 发现的“运行库保留高水位直到最后一个 NR 句柄释放”，多出来的实例会一直占着显存。
   - **在途纹理按槽复制。** 我们每个 context 有 `NrSlot slots[8]`（`:5605`）；只有所有已占槽都在途时才新建槽，context 存活期间只增不减
     （`FindOrCreateSlot` `:8766-8796`，只在 CreateSlot 失败或 context 销毁时回收 `:5942`）。对方每个 owner 只有一套纹理，
     靠 fence 退休保证安全，不按在途帧复制（`S/DlssNr_Dx12_Resources.cpp:4-11`，`S/DlssNr_Dx12_Models.cpp:49-60`）。
     4K 后置 100% 时我们主层每槽约 221 MiB，每个后层每槽再加 63–95 MiB（见第 3 节）。**实际占了几个槽，F8 不报。**
   - **后层重复做导引。** 每个后层各自做一遍 MV 重采样和 RR depth（`src/nr_dual_runtime.inl:150-161,277-283`），尺寸和来源与主层相同。
4. **反向差异，我们更稳：**
   - 对方重建时先建新 Feature，等旧的 fence 完成后才释放（`N/DlssNr_Proxy.cpp:132-197`，`N/DlssNr_GpuLifetime.cpp:212-255`）；
     多层每个提交周期只重建一个，重建窗口是 N 帧（`S/DlssNr_Dx12_Models.cpp:134-178`）。
   - 我们对同一句柄先退休排空再创建（`:6407-6434`，#306）。跨句柄仍可能重叠：deferral 只检查被请求的句柄（`:6413-6415`）。
   - 对方 NR 关闭时也不释放 Feature（`N/DlssNr_Pipeline_Dx12.cpp:144-146`），与我们 F6 保留缓存相同。
   - 两边创建都不看 DXGI 显存预算；我们只要求至少 1 GiB 系统 commit 余量（`:10408`，`:10424-10433`）。
5. **能大幅降显存的只有两类：少建或早放模型实例，缩小模型尺寸。** 后者是否省显存（而不只省时间），两边都没人测过。
   这是第一个要回答的问题，否则“减像素省显存”只是猜测。

## 2. 对照表

| 维度 | 本项目（Beta 13 源码） | 对方 v0.8.91 |
|---|---|---|
| NR 实例 | 每层一个；最多 5 层（`src/nr_layer_config.h:9`）；层间不共享（`src/nr_dual_runtime.inl:169-172`） | 每 pass 一个；默认 1，菜单 2/10 |
| 重建顺序 | 同句柄先排空再创建（#306/#319）；减层或显式改路线时整体排空 | 先建新、后按 fence 释放旧；无上限等待 |
| 路线缓存 | 前置、后置、SR/RR 句柄可同时驻留 | 每 owner 一套；placement 变化全部退休 |
| 在途纹理 | 每 context 最多 8 槽，只增不减 | 单套；fence 退休后复用 |
| 模型尺寸 | 前置默认 N=R，预设 0.58–0.33×O，钳位 0.5–1.0R（`:9744-9760`）；后置 O×百分比 | `WorkingScale` 0.25–2.0（`S/DlssNr_Dx12_Run.cpp:126-132`）；可选外围压缩，**会缩小 NGX Feature 创建尺寸**（`S/DlssNr_Spatial.h:149-153`，`S/DlssNr_Dx12_Models.cpp:147-148`） |
| 导引 | depth 直传；MV 尺寸不等于 N 时重采样到 MotionProxy（`:9945-9952`）；RR 路线复制 depth | depth/MV 直传原资源加各自 subrect，MVecScale 换算（`N/DlssNr_Proxy.cpp:222-241`）；只对 typeless 复制 |
| 预算准入 | 系统 commit ≥1 GiB；不看 DXGI | 无 |
| 显存诊断 | F8 有 DXGI 进程用量/预算、后层纹理、context 计数、运行库文件哈希（`src/nr_feedback_modules.inl:65-86`） | 无显存数据；每 600 帧记录 NR 时间均值/p99（`S/DlssNr_Dx12_Status.cpp:82-91`） |
| 运行库 | 20/30 plain FP16 `6dac1b40…`；40/50 Kio v1.0.6 `a2d16f9f…` | RTX50 原版 `E16BCF15…`；20/30/40 ShortFuse `E67DEE20…` |

## 3. 本项目自有纹理估算（未计对齐，不含 NGX 内部）

单张大小：4K RGBA16F 63.28 MiB，RG16F 31.64 MiB；2304×1296 RGBA16F 22.78 MiB。

| 情形 | 每槽纹理 | 每槽 MiB |
|---|---|---|
| 2304×1296 前置主层 | Proxy + NeuralOutput + Result | 68.34 |
| 2304×1296 后置主层 | SRIntermediate + Proxy + NeuralOutput + MotionProxy | 79.73 |
| 4K 后置 100% 主层 | 同上 | 约 221.5 |
| 每个后层 | SecondAnswer（+ 自己的 MotionProxy） | 4K 63.28（94.9）；2304×1296 22.78（34.17） |

示例：4K 后置双层，若实际占 3 个槽，自有纹理约 0.94 GiB。对方同条件只有一套纹理，约 0.3–0.4 GiB
（colorCopy/hdrCopy/output 等，多 pass 另加 passScratch/passClamp）。按 #319 实测，每个模型实例约 0.7 GiB，
所以纹理是第二量级，但在 4K 下不可忽略。实验平面和诊断缓冲在默认关闭时不分配（各自预算见 #298 2026-09-30 评论）。

## 4. 显存建议：套进 #298 已有的六步，不另起计划

1. **账本（第 1 步）要补的具体项：**
   - 主层槽数和字节。现在只统计后层纹理（`:6461`）。
   - 按 mode、句柄、层分列的 context 数。
   - `g_extra_allocated_bytes`，现在只写日志（`:8435`，`:8544`）。
   - 隔离 context 的字节数，以及高水位。
   - 每次 NGX Create/Release 前后的 DXGI local 差值，用来在游戏里实测单个实例的代价，而不是套用 5090D 的数字。
   - 另加一条定时趋势日志（NR GPU 时间 p50/p99、DXGI 用量/预算、live/retired/pending 计数），补上 F8 单点快照看不到的增长过程。
     对方的做法见 `S/DlssNr_Dx12_Status.cpp:82-91`。
2. **离线测量（第 6 步的具体做法，最省事，先做）。** 现成的 `tests/post_scale_create_ngx_host.cpp` 已在每个阶段记录 DXGI local 用量（`:11-23`），
   并循环 3 个尺寸（`:136`）。改参数跑三组：
   - 串行创建 100/90/81/50% 尺寸：回答模型显存是否随尺寸变化，决定外围压缩和降工作比例是否值得做。
   - 同一序列换 Kio、plain FP16、原版 310.8（以及拿得到的 ShortFuse）四个运行库：量化运行库本身的差别。
   - 12 次以上换模型（串行与重叠）：必须回到基线。对方 v0.8.4 用的就是这个判据。
3. **关闭的实验不分配（第 2 步）。** 源码显示默认设置下每槽只有模型纹理和两个 192 B 统计缓冲，用账本确认即可。
   对方打包脚本在默认 INI 开启任何实验或带游戏专用进程过滤时直接失败（`package_release.ps1:86-91`），且拒绝覆盖已有版本号（`:13-15`）；可核对我们的门禁是否已有同等检查。
4. **新增：只保留当前路线的模型。** 自动路线回退、SR↔RR 句柄切换时，也按现有 recording/fence/binding 证明退休非活动 mode 的 context。
   F6 只保留当前路线。验收：前置→后置→前置序列下，账本显示旧路线实例数归零，DXGI 用量回落。
5. **按预算准入和降档（第 3 步）。** 创建前核对 `QueryVideoMemoryInfo` 的余量，单实例代价取第 2 项的实测值。两边都没有，我们可以先做。
   自动降档只影响本次运行，不改保存的设置。
6. **多层共享中间纹理（第 4 步）只做有证据的部分：**
   - 后层的 MotionProxy 和 RR depth 与主层同尺寸、同来源，可以考虑共享，但要先补寿命证明。
   - 层输出 ping-pong：对方在 pass 之间轮换 output/passScratch（`S/DlssNr_Dx12_Run.cpp:415-449`），并有双层游戏记录。
     我们 2026-09-22 的审查（`nr_owned_scratch_lifetimes_20260922.md`）因“不透明 Feature 可能保留引用”否决了这一做法。
     要先用真实运行库做 A/B（轮换对比独立缓冲，输出逐位比较）再定。收益小：2304×1296 下每个后层每槽 22.78 MiB。
7. **按实际在途帧限制槽数（第 5 步）：**
   - 先用账本拿到真实槽数。
   - 再做“空闲槽在完成证明后回收”：现在只增不减。
   - 更长期的方向是把廉价的每帧状态（描述符堆、绑定）和昂贵的纹理分开。我们的 Present 路线已在同一真实队列上串行复用 scratch
     （`nr_compat_present_events.inl:92`），对方也是单套纹理。前提是证明所有消费者在同一队列；这需要设计审查，不是小补丁。
8. **外围压缩只作为默认关闭的实验，且要等第 2 项证明有收益：**
   - 来源必须用 BeliyG3 的 MIT 上游（`sdk/src/math.cpp`、`sdk/src/layout_v2_sides.cpp`、`sdk/shaders/ofps_common.hlsli`、
     `sdk/shaders/ofps_pack.hlsli`）并保留声明。不用 wilsjo2 的 GPL shader。
   - 对方证据是离线真实 NR：4K 下 pack+模型+unpack 时间低 10.6–12.3%（`docs/NR-SPATIAL-COMPRESSION.md:44`），没有游戏数据。
   - BeliyG3 插件自己的压缩纹理在 4K 约 0.6–1.0 GiB（5 个 pack 槽），实现不当会把省下的显存吃掉；wilsjo2 集成版只加 3 张压缩平面和 2 张展开平面。
   - 不照搬 BeliyG3 的几处做法：
     - 同尺寸改布局不重置历史（`core/frame/lifecycle.cpp:104-106`），违背逐层历史身份。
     - 对模型扣留 UI 并设 `UICorrection=0`，与 #317 冲突。
     - 每进程只压缩一个 Feature，无法用于多层。
     - 跳帧时对 NGX 报成功，会让 F8 看不到旁路。
9. **不建议现在做隔帧推理和残差重投影：**
   - BeliyG3 自己的 compute 时域移植没有通过它自己的验收（`docs/dev/temporal-validation.md:3-5`）。
   - 它在 16 次 evaluate 后用 CPU 直接 signal 被放弃的 fence（`core/gpu/submission.cpp:139-141`），违背完成证明规则。
   - wilsjo2 也明确排除了半帧率 NR（`docs/RELEASE-v0.8.5.md:32`）。
   - 把 BeliyG3 插件直接挂在我们 Core 上测试也不可取：它直接 hook `nvngx_dlssnr` 导出，每进程只处理一个 Feature，自有纹理还会掩盖显存变化。

## 5. 显存以外值得学的（按对我们开放问题的价值排序；GPL 部分只学思路）

| # | 做法 | 对方位置与证据 | 对应我们的问题 |
|---|---|---|---|
| 1 | 把离线宿主改名为 `Cyberpunk2077.exe`，触发驱动的按程序 profile 复现创建失败。对方以此发现 CP 拒绝 LDR RR Create（`0xBAD00005`），加 `IsHDR` 后修复 | `tests/run_nr_private_upscaler_smoke.ps1:48-50`，`docs/NR-PRIVATE-RR.md:9-13`；真实运行库，仅 Feature 13 | 2077 首次 NR Create 失败：用我们的 `post_scale_create_ngx_host` 改名复现，再逐项比较创建输入（我们用 `AllocateParameters` `:11138`，对方用 `GetCapabilityParameters`；我们 `PerfQualityValue=5` 加游戏 flags `:10209-10210`；对方设 `Hint.Render.Preset` 和显式空 UI 键） |
| 2 | 接入 NGX 自身日志（FeatureCommonInfo 的 logging 回调），并在创建时报告 adapter/命令列表 LUID 是否一致、设备移除原因 | `N/DlssNr_NgxDiagnostics.cpp:22-43,131-180`；只有代码，且其 `Install()` 未接入 | 我们 Init 传 `nullptr`（`:2618-2619`，`src/nrchain_nvngx.cpp:35-37`）。有了 NGX 日志，2077 失败原因可能直接可见 |
| 3 | 换模型循环必须回到基线的回归测试 | `docs/RELEASE-v0.8.4.md:11`，`tests/nr_gpu_lifetime_smoke.cpp:288-332` | #298/#293 长时间增长；并入第 4 节第 2 项 |
| 4 | 分阶段创建：每个提交周期只补建一层，只运行已就绪的连续前缀，缺层不借用第一层历史 | `S/DlssNr_Dx12_Models.cpp:133-178`，`S/DlssNr_Dx12_Run.cpp:342-355`；游戏 1–2 层 | #293 首开卡顿：把多层 Create 分散到多帧，可能拆开单个尖峰（注意这也会延长新旧重叠，需与第 4 节第 4 项一起设计） |
| 5 | 成对标注的“效果丢失/效果存在”抓帧（Ctrl+F8/F9，每次 4 帧，带 subrect、reset、jitter、MV 缩放、曝光清单） | `docs/NR-PHOTO-DIAGNOSTIC.md`；WARP | #317-A Esc 过渡：再加一个按帧的提交、跳过原因、Reset 小环形记录，触发时回溯。两边都还没有 |
| 6 | “应用模型”关闭时模型照跑、只隐藏结果，得到同成本的画面对比；F6 会同时改变成本 | `N/DlssNr_Menu.cpp:95-97,153-157` | #277 成本与画质评审 |
| 7 | 基准协议：正反两种顺序跑，排除每个新 Feature 前 2 次 evaluate，报中位数/p95 和配对 PSNR，manifest 记运行库和 shader 哈希，明确“不是 FPS” | `tests/spatial-benchmark/README.md:9,18` | 用于 `tests/temporal_cost_real.cpp` 和第 4 节第 2 项，加上逐层时间 |
| 8 | 导引一帧不可读：只跳过该帧并重置历史，恢复后自然重试，不锁死旁路 | `docs/NR-RESTRICTION-AUDIT.md:11`；只有代码 | #317-C 同族：审计瞬时导引丢失不会变成持续旁路 |
| 9 | 每个修复写明“没有它哪个测试会失败”；发布说明短，并单列“未验证/不声称” | `docs/NR-GPU-RETIREMENT.md:19,25`，`docs/RELEASE-v0.8.6.md:5,17` | PR 规范 |
| 10 | 冻结帧（固定输入和导引，只改设置） | `N/design/frame-hold.md`；离线 | #309 细线质量门、空间实验的同源对比 |
| 11 | 增益与色度分开缩放（Transfer=3/4），避免头发/皮肤边缘光晕；1/64 线性光载体缩放消除 KCD2 FP16 暗部斑点 | `docs/RELEASE-v0.8.8.md:16-22`（WARP，未开游戏）；`docs/NR-DLSS-ENLARGEMENT.md:15`（游戏） | #309 低网格光晕、私有 SR 载体精度 |

## 6. 不照搬，或我们已经做得更好

- **跨队列等待：** 对方 v0.8.5/v0.8.6 加的跨队列呈现等待导致 Cyberpunk 卡死，v0.8.7 撤回（`docs/PR-REWRITE-REVIEW-v0.8.5.md:5`）。
  我们一直拒绝用私有等待去桥接未解决的队列。
- **就绪判定：** 对方以提交 epoch 或完成探测判定 Feature 就绪（`N/DlssNr_Proxy.cpp:89-94`），弱于我们的实际提交加精确 fence，以及隔离到进程退出。
- **签名检查：** 对方的直连运行库通过改写 NR DLL 的 `GetModuleFileName` 导入绕过签名加载检查（`docs/NR-DIRECT-RUNTIME.md:5-9`），
  违反“不绕过既有验证”。我们的 nrchain 调用者路径做法不改 NVIDIA 代码。
- **运行库门：** 对方已删除运行库哈希/版本/大小门（`docs/NR-RESTRICTION-AUDIT.md:14`），哈希只在文档里。我们 F8 记录实际加载文件的哈希。
- **INI 键：** 对方保存时删除已移除的 INI 键（`OptiScaler/Config.cpp:1276-1285`），会破坏回退。我们应只在读取时忽略。
- **测试方式：** 对方关机测试靠字符串提取函数体（`tests/run_nr_shutdown.ps1:6-17`）；我们的宿主编译完整生产翻译单元。
- **显存数据：** 对方 NR 代码里没有任何显存数据；我们已有 DXGI 进程用量/预算、commit 准入、父子退休传播和隔离计数。

## 7. 下一步执行（Windows，执行 agent）

1. 第 4 节第 2 项的离线测量，产出同一宿主、同一 5090D 下“尺寸×运行库”的单实例 DXGI 差值表，以及循环换模型的基线曲线。
2. 账本 PR（第 4 节第 1 项），不改渲染行为。
3. `Cyberpunk2077.exe` 改名复现，同时接 NGX 日志回调（第 5 节第 1、2 项）。

维护者收到“某某项目更省显存”的反馈时，请按同一场景收集双方的 F8 或覆盖层显存读数，以及运行库哈希、层数、分辨率、前置/后置、FG 开关。
缺任一项都不能下结论。

## 8. 证据边界

只做了源码审查，行号对应上表所列 SHA。未编译，未在 WARP、NVIDIA 或游戏上运行。纹理大小按无对齐公式计算，不含 NGX 内部分配。
746 MiB 与 3.5 GB 来自 #298 中 #319 的受控 Create/Release 记录，不是游戏读数。对方引用的所有数字均为其文档自述，本页未复现。
