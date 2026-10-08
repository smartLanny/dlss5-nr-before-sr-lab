# OptiScaler 两 RR 与 raw-guide 近似路线复核

2026-10-08；只读源码审计与本项目既有证据对照。本轮没有运行第三方二进制、
GPU 或游戏，没有移植 GPL 实现，也没有改变产品接线。

## 当前固定身份

已实时通过 GitHub API 与 `git ls-remote` 交叉核实：

| 对象 | 当前身份 / 状态 |
|---|---|
| 上游 main | `1bd39091337cc07ba961c8e59ded57e21dc95b18` |
| 最新上游 prerelease v0.8.91 | `f45ccf3a761df91450959d325ac0166cba5364c9`，2026-09-23 |
| [PR #123](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/pull/123) | open；`8fbdfa60df631c1f41bd39cf6afcbd96d76f1979`；实际作者分支 `GrimsVerk/denoise-first`；以 v0.8.91 树为基础 |
| [PR #109](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/pull/109) | open；`57aa64e03c3e6f74903fbd1d550748904cb9fd58`；独立的 reduced-guide/MV 修复，没有混入 #123 |

main 与 v0.8.91 没有 #123 的 DenoiseFirst 实现。新模式整体默认关闭；开启后
默认 step=2、ratio、Catmull-Rom、shift=-NGX jitter、firefly guard 开启。
NR 默认单层、WorkingScale=1、SpatialCompression=false；PR 作者性能表使用
WorkingScale=.85 与空间压缩，不能把该测试设置写成代码默认值。
见 [Config.h:256-292](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/Config.h#L256-L292)
及 [Config.h:354-379](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/Config.h#L354-L379)。

## 实际调用链

| 阶段 | 颜色 / extent / 相位 | guide 与调用 |
|---|---|---|
| 第一遍私有 RR | raw Xj，R→R；输出 C，被实现视作稳定网格 | Feature13 独立 handle；DLAA5；游戏 depth/MV、7个 RR guide 资源及矩阵原样传入 |
| NR | C 的副本；默认 N=R，得到 edited A | Feature18；直接复用原始 depth/MV；没有 P0 producer，没有 NR normals/matrices/jitter 参数 |
| 编辑移回 raw | 先逐 tap 形成 ratio，再按 -jitter 重采样 | composite 的 extent 与原始 raw 同为 R；原始 raw 不被 NR 改写 |
| 游戏 RR | 仅 Color 改为 composite；R→O | 原游戏 handle/history；原 jitter、depth/MV、其他 guides 与矩阵保留 |

第一遍创建明确写 R/R 和 DLAA，继承 inverted/MVJittered/MVLowRes/HDR/
AutoExposure、roughness/hardware-depth 模式。私有 adapter 新分配参数表，
没有显式复制或设置游戏 RR preset，故不能说第一遍完整复制了游戏模型选择。
见 [DenoiseFirst.cpp:310-329](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_DenoiseFirst.cpp#L310-L329)
及 [Upscaler.cpp:176-208](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Upscaler_Dx12.cpp#L176-L208)。

RR guide reader读取 diffuse/specular albedo、normals、roughness、reflection MV、
specular/diffuse hit distance及独立subrect；读取并复制 WorldToView/ViewToClip
矩阵值，检查 finite，不从矩阵中移除 jitter。RR 首遍 Eval 转交原始 jitter/MV scale、
frame time、preExposure/exposureScale 和这些 guide/matrix。
见 [Upscaler.cpp:19-75](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Upscaler_Dx12.cpp#L19-L75)
及 [Upscaler.cpp:341-372](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Upscaler_Dx12.cpp#L341-L372)。

创建 flags 掩码包含 DepthInverted/MVLowRes/MVJittered/IsHDR/AutoExposure。
该模式拒绝 display-resolution MV、非零 color/depth/MV/output origin、非 direct
command list及不能恢复的命令状态；RR guides 不可读时会退到 private SR，并警告
不能去除路径追踪噪声。这是实现的 fallback，不能登记成两 RR 成功。
见 [DenoiseFirst.cpp:186-249](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_DenoiseFirst.cpp#L186-L249)
及 [DenoiseFirst.cpp:279-299](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_DenoiseFirst.cpp#L279-L299)。

NR 输入是 clean 的 copy，直接调用 `Run(cmd, edited, depth, motion, edited,...)`。
depth/MV 仅变 barrier state；没有 P0 几何重绘，没有相位平移或可见性重建。
NR binder只设置 Color/Depth/MVec/Output、各自regions、DepthInverted/Reset及
MVecScale。没有把 RR normals 或矩阵送入 NR，也没有为 NR 解消 MV jitter。
见 [DenoiseFirst.cpp:468-516](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_DenoiseFirst.cpp#L468-L516)
及 [Proxy.cpp:222-248](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/dlssnr/DlssNr_Proxy.cpp#L222-L248)。

NR 使用自身颜色 codec：HDR 默认 manual white point=1/soft knee/linear→sRGB，
后续 resolve 将编辑还原至 clean 的场景颜色域；可选游戏 exposure 或自行 meter
只影响 codec，ExposureTexture不直接绑定至 Feature18。首遍 RR 得到游戏
exposure；若没有 texture 且未开 AutoExposure，才给 unit texture；同时显式
传游戏 PreExposure/ExposureScale。
见 [DenoiseFirst.cpp:412-450](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_DenoiseFirst.cpp#L412-L450)
及 [Encode.cpp:96-176](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_Encode.cpp#L96-L176)。

WorkingScale≠1 时 #123 只缩放颜色，普通路径保留 raw guide regions，MV scale乘
work/frame比例。可选空间压缩另按 warp采样 raw depth，并把 MV 的两个端点映射
到 packed grid；它不是 P0 producer。独立 PR109新增 point resize到工作尺寸
及以实际 motion texture / MV-reference extent计算scale；也不是 clean visibility
重建，不能把它写成 #123 已有能力。
见 [Run.cpp:273-333](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp#L273-L333)、
[Run.cpp:336-422](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp#L336-L422)、
[spatial shader:94-111](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/precompile/dlssnr_spatial.hlsl#L94-L111)
及 [PR109](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/pull/109)。

多层 NR 每层独立 Feature18/参数/历史，默认一层；额外层继承参数但 LocalTone
默认0，逐层 ping-pong，层间 clamp proxy，最终只compose一次。其 binder 的
preset=0/intensity=1默认，与本Core现有Quality5/intensity1.5不是同一参数事实。
见 [Proxy.cpp:50-76](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/dlssnr/DlssNr_Proxy.cpp#L50-L76)、
[PassProfiles.h:9-38](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/dlssnr/PassProfiles.h#L9-L38)
及 [Run.cpp:415-453](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp#L415-L453)。

## 编辑算法与实际调用次数

默认 shader 先逐 tap形成 `(max(A,0)+.001)/(max(C,0)+.001)` 并截到
`[0,MaxRatio]`（默认2），再 Catmull-Rom 重采样；边界clamp、权重归一。
默认 firefly guard用同索引、未移相C的亮度与raw亮度构造ordinary，减弱
亮样本上的gain；最后 finite/nonnegative处理并保留 raw alpha。因此它是
ratio-before-shift家族的一个带偏置与保护的具体近似，不等于本项目现有P公式，
也没有本项目要求的原始位型拒绝退回证明。
见 [denoise shader:73-81](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/precompile/dlssnr_denoise_first.hlsl#L73-L81)
及 [denoise shader:121-176](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/precompile/dlssnr_denoise_first.hlsl#L121-L176)。

step2只改 Color，保留游戏 jitter；step0把A交游戏且临时jitter=0；step1先做
private SR，但仍会执行游戏 EvalInternal，然后After覆盖游戏Output。
因此在 RR 游戏内，step2/step0各两次RR；step1是两次RR再加一次私有SR，
不能按界面名称少计成本。第二遍游戏表原有参数与历史延续；原 Color在调用后
恢复，step0的 jitter 由RAII恢复。
见 [DenoiseFirst.cpp:539-624](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_DenoiseFirst.cpp#L539-L624)、
[IFeature.cpp:305-353](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/upscalers/IFeature_Dx12.cpp#L305-L353)
及 [DenoiseFirst.cpp:628-673](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_DenoiseFirst.cpp#L628-L673)。

## 时域、资源与已验证程度

没有外加编辑 accumulator；第一遍RR、每层NR、游戏RR本身都有独立时域历史。
第一遍创建以later submittedEpoch准入；NR另有CompletionProbe，但其ready也
接受epoch变化。退休另有真实queue-fence及Reset/release-recording跟踪，失败
fence/removed-device不完成，未解决闭包在tracker销毁时保留。不是纯frame-age
退休。generation按device/extent/format/flags/quality/roughness/depth变化替换，
step切换保留首遍history；gap/未配对After会设置reset。不能把其epoch创建
准入替代本Core已有精确recording/owner/不透明状态恢复证明。
见 [DenoiseFirst.cpp:76-108](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/shaders/dlssnr/DlssNr_Dx12_DenoiseFirst.cpp#L76-L108)、
[Proxy.cpp:88-104](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/dlssnr/DlssNr_Proxy.cpp#L88-L104)
及 [GpuLifetime.cpp:169-253](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8fbdfa60df631c1f41bd39cf6afcbd96d76f1979/OptiScaler/dlssnr/DlssNr_GpuLifetime.cpp#L169-L253)。

PR作者报告巫师3/5080实测和局部GPU timer，评论另有4080使用者反馈；这些是
有明确范围的第三方观察，没有本项目复现、原始可核回执或通用兼容性结论。
首遍输出相位稳定、raw-guide适配与模型收益仍属需实验的假设，不能从截图或
Create/Eval代码存在推成Core验收。参见 [PR123正文及评论](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/pull/123)。

## 对本项目的修正与最小接缝

旧thinstrip反例继续证明：raw采样不能唯一恢复精确P0可见性。它没有证明
`NR(clean, raw guides)`这一近似算法不能运行或不能有质量收益。故“缺精确P0
producer则禁止所有两RR实验”过强；应改为禁止将raw-guide近似登记成精确
clean-guide合同。资源身份、同步、曝光、状态与拒绝位型门继续独立严格。

第一遍R→R DLAA已有本项目准确旧tuple的official optimal及32次真实child
Eval记录，见 [SceneRR报告](RR_SCENE_BACKEND_PROBE_20261008.md)。
`tests/scene_rr_probe_host.cpp.in:148-195`查询目标R、DLAA5、RTXValue0并检查
callback返回的input==R；202-225设置R/R、DLAA专属preset0并创建独立child。
这支持复用固定1280×720切片，不外推所有尺寸/运行库/游戏。
原生R1280×720→O1920×1080则复用
[one-RR功能切片](RAW_NR_ONE_NATIVE_RR_FUNCTIONAL_20261008.md)的真实parent、
MaxQuality2专属preset0、原始同域cap/optimal、Color事务与consumer lease。

新增私有实验只需一个新mode，N=R、单层NR、空间压缩关闭，明确标记
`clean-color-with-raw-guides-approximate`，独立child与真实R→O parent，不二次Init。
增加clean/edited/composite三张R资源；child写clean，生产NR/codec读clean并写edited，
自有正权双线性ratio-before-shift写composite，再复用现有Color-only nativeRR事务。
两次RR及NR/codec依赖纳入同一真实recording闭包，在最后consumer完成及成功Reset
后一起退休。复用现有厂商失败quarantine/_Exit，不造新GPU提交账本。

有界首批只准备CPU/编译和关键diff：固定J序列、16warm+16observe；基线
raw→一次RR，对照已有rawNR→一次RR，新臂twoRR+NR(raw guides)，每臂独立历史，
新臂预期childEval32/nativeEval32/NR单层Eval32。不添加尺寸扫描、参数搜索或游戏
动作。计时以后分开记录首遍RR、NR/codec/compose、末遍RR与整帧；新增RR成本
应与同N/层数的rawNR一次RR比较，不能仅与4K NR比。

最少CPU反例覆盖：identity原始位型退回、ratio-before-shift与先插值再除的区分、
无效正权tap/非finite/非正分母/越界的拒绝、jitter符号固定。GPU运行与模型相位、
一般3D遮挡、画质/性能、产品接入仍各自记录，不能用上述准备稿预先判通过。
