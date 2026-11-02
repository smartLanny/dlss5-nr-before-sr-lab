# 私有 two-RR approximate 实验 v1

本包只完成 CPU 准备，不改变产品源码/游戏文件，不运行 GPU/NGX。独立代码仅借鉴参考项目的算法顺序，未复制 GPL 实现、插值代码或其保护策略。

固定三条新进程历史 `baseline` / `variant` / `two-rr-approx`；每条 J 32 帧，16 暖机 + 16 观测，R=N=1280×720，O=1920×1080，不改变输入/质量/层数。至多 3 个进程、96 原始 parent RR Eval、32 私有 child RR Eval、64 NR Eval；遇到首个失败停止，无同帧 fallback/retry。

`baseline` 和 `variant` 复用已实际通过的 oneRR 主循环。新路线：raw R → 独立 DLAA RR R→R → clean R → 单层 NR N=R → edited R → 逐 tap ratio 后正权双线性以 `-j` 搬移 → 乘 raw 成 composite R → 原始 parent RR R→O。三张 clean/edited/composite 资源分别独立。原始 parent 的 depth/MV/normal/roughness/albedo/matrices/exposure/jitter/subrect 等参数完全沿用 frozen 实际 oneRR getter 合同，只替换 Color。

私有 child 使用保留的原始 Core/device/init domain，无第二次 native Init；真实 capability/official optimal 必须再次返回固定 R→R DLAA。child 质量 5/default DLAA preset 0，parent 质量 2/default Quality preset 0，flags75，reversed hardware depth，MVScale1，PreExposure .75，ExposureScale2。child 吃原始 raw jitter 和完整 RR guides；clean NR 的 color jitter 明确 0，depth/MV 则沿用 raw producer，**可运行但 visibility/时域语义是 approximate，绝不标记 exact clean guides**。此包不证明 clean 输出满足所有 NR 数值范围。

每帧同一真实 recording 内保留 child、NR slot/codec/capture、composition、final RR 全部实际 packet。所有 packet 必须指向相同真实 queue/fence/value；完成 fence 后仍不可变，成功原生 Reset 后才允许 Drop/Release。opaque Create/Eval 各自独立 capture/restore 当前 command state，失败保留闭包并 quarantine。没有引入 epoch 准入或替代提交账本。

合成先在同 recording 内 `CopyResource(raw→composite)` 保留完整原始 FP16 位型。shader 对 identity 或拒绝域 return，不写该像素。只对合法非identity像素写 RGB；Alpha 维持 raw。拒绝域：正权 tap 越界、非有限 phase/C/A、非identity C 任一 RGB<=0、A<0、非有限 ratio、raw RGB 非有限/负数、raw alpha≠1、结果非有限/负数/超过 FP16 65504。alpha=1 是本次不透明 raster 的有界输入合同；所有其他 alpha（包括 NaN payload）整像素退回。零权 tap 不参与，C=A 的有限 tap（含零）定义 ratio1。没有 epsilon、clamp、firefly guard、负权或边界 clamp。CPU oracle 与实际合成读回逐像素对照；拒绝/identity 位置必须保留全部 raw 位型，合法编辑允许最多1 FP16 ULP，额外保存拒绝覆盖度供画质判断。

`CPU_RATIO_CASES.json` 是小反例/位型 oracle；`COMPILE.json`/`SHADER_COMPILE.json` 仅 CPU 编译；`critical.diff` 是相对 frozen oneRR 的私有源码变更。`SOURCE_IDENTITY.json` 是 inherited 冻结 Core 身份，`PACKET_IDENTITY.json` 区分当前私有host身份。此 host 链接 frozen canonical-r3 assembled Core，不能声称对应当前产品 head。

后续由 root 在 Astra high 审查后串行执行 `python run_two_rr.py --root-two-rr-go`。固定新输出目录 `../two-rr-approx-v1-r1`，存在即拒绝。当前本包尚未 GPU 运行，质量/性能/游戏验收均 unknown；原候选不包含新 two-RR 功能。参考截图不能作为本实验验收。
