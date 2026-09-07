# 统一开发与测试交接入口

## 2026-09-08 Feeder provider 独立开发补片（不进入玩家包）

分支 `codex/provider-transport-20260908` 从最新 main 接全量 #166 热修后，
恢复既有 Feeder 两提交；没有新增 Opti 依赖。现有 VS2022 已完成固定 Feeder
完整链接，旧“缺 MSVC/链接失败”不再是当前阻断。新增 Signal/Drain/同帧
完成保护与 F8 provenance/recorded 状态；生产函数故障13断言、完成控制/WARP
22、原准入46/Windows客户端23通过。原生热修40项统一门禁也通过。
完整 consumer/F8 核心实际编译源码 `07c219c`；producer 实际构建 `48598dc`。
NVIDIA Feature18 与无DLSS游戏尚未验收，不宣布新游戏支持、不覆盖 Release。
[实现、精确证据和剩余宿主接入](FEEDER_COMPLETION_20260908.md)。

## 2026-09-08 0.4.6 发布后界面补正（当前接续）

接续 PR #166，修复未定义 `NR_HAS_F8_SEQUENCE` 使主设置页 F8/F7 控件在实际
Release 中被排除，以及独立 F8 Capture overlay 内嵌新窗口导致空白/闪现。
采集后端与热键原本仍在；06:57 异环真实 F8 已找到，不能说 F8 没有生成报告。
反馈与诊断合并，渲染比例实验收进画质底部开发者选项，默认效果倍率改为 1.0。
[本批根因、实现和验收边界](FEEDBACK_UI_FOLLOWUP_20260908.md)。
独立 `0.4.6-hotfix.1` 候选实际构建源码 `35ef9a8`，Core PE `0.4.6.1`，
40 项本地统一门禁通过；中文 OTA 7 文件、685762 bytes，SHA256
`bd0ebd2f94041a7fee73aa4d77f48c914b833a0eec9f05b12e41419281e5dc77`。
新候选游戏画面待 ROG；中英文 Release 实际 DLL 的 F8/F7 控件已回查存在。

新 F8 显示人脸开关后参数已重建并继续提交，尚未证明模型正确识别人脸；
真正的脸部独立最终倍率需要 mask 数据，不能以参数送达冒充画面修复。#162
继续保留。以下 39 项构建证据属于原发布源码，不自动覆盖本次补丁。

原 beta0.4.6 已发布，核心构建为 `9087a9e`；Manager 发布源码为
`fb96c022b0a86e72da49a3a59778517daa7e0126`，按游戏兼容桥接已实现并经
144 项本地测试验证。现有发布包和 tag 保留，不因本次界面补正自动覆盖。

## 2026-09-08 0.4.6 新候选（最新）

实际构建源码 `9087a9efbc7bb53a3c79e7766a174534f49c412c`，Core PE 0.4.6.0，
39 项本地 Windows/编译/WARP 门禁通过。中文版 OTA 为 7 文件、688999 bytes，
SHA256 `6f633f141a7ba80e2a2d87f440cc9cf1da36233c27b95d73f20445e7f13d9117`。
[完整改动、证据与哈希](UI_RR_FEEDBACK_REFRESH_20260908.md)。

第二份 2077 F8 已明确 Feature1 到达 hook，但被保留 RR 句柄反复旁路，现修复
缓存句柄阻断与实际事务保护的混用；仍保留并发/嵌套 abort、生命周期库存和
真实 RR 后置。异环新的人脸对照单 #162 已修低分辨率残差扩大，并给后置加入
原始光照保护；没有承诺模型自身所有变暗都消失。#124/#162 新候选画面与热切
仍待 ROG；不是重新处理旧异环混线单。

UI 标题“装机宅DLSS 5”，二群 392308850；主要控件常展开、滑块同行、NR
50–100%，游戏渲染请求移实验页。RR 自动后置，F8 单 TXT 自动复制路径，F7
512 ROI 和前后置分辨率矩阵。首份 F8 轮询已删除。0.5 NR/RR 性能实验已补 #15。

Manager #161 已独立合入，hotfix.3 发布分支为
`product/xiaofeng-manager-foundation@4efa5f03345fa12d90b209d041eb549893d661ed`。
新的按游戏“兼容桥接”开关及 0.4.6 发布由维护者指定 Manager 线程接续，先核对
既有实现。#153 架构问题不扩大；本批保留 #149/#150/#151/#155/#156/#157/#160
全部已有修复，#154 不再双合。main 未被本核心任务自动合并，CI 未启动。

以下为前一阶段记录，遇到版本/证据冲突以上方 0.4.6 节和 #80 最新评论为准。

## 2026-09-08 本地候选收口与 carrier 排除

本轮从 `main@0afae4b` 接续，未合并 main、替换已发布 tag 或安装游戏。
**有限 blend 准入只保留 #149；#154 已关闭为重复候选**。#149 已含
#154 的完整测试/构建锁及细分 reason；不要双合。#150 以 #149 分支为 base，
仅保留 RR 边界诊断，待 #149 合入后再改回 main。#151 LightingLock、
#155 文件名识别、#156 signedWin32=126 分类、#157 source=off 私有设备
bootstrap 分别独立；#155/#157 都不是 #153 游戏崩溃修复。

上述六个保留候选已做 Windows compile/WARP 收口；组合构建源码仍为
`a2a338012a97f98f9327c141f32e274989602eb1`，36 项统一门禁通过，
后续 `aa88da0` 仅为文档/合并交接。具体命令和哈希见
[组合验证记录](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/codex/priority-validation-stack-20260907/docs/interop/PRIORITY_VALIDATION_STACK_20260907.md)。
关闭 #154 没有改变该组合二进制。CI 因付款/额度未启动，不重跑、不计通过。
#99/#123/#153 保持 open + needs-evidence；待 ROG 验收对应新候选，
不重复索取已有旧包证据，也不阻塞独立 PR。

#153 本轮选择先交付 [ReShade 预加载排除工具](RESHADE_CARRIER_EXCLUSION_20260908.md)：
明确 D3D12/Vulkan profile 下使用现成 `DisabledAddons=@实际文件名`，
在下次普通扫描中阻止映射；12 项 Python 和 12 项真实 Windows parser/DLL
测试通过。DX11/未知 API 不自动改动，direct-load carrier 配置拒绝写入。
Manager 默认不部署无关 carrier 由独立任务推进。自动 DX11 激活、早期 NGX
Create 和多设备生命周期仍是另批评估范围，不能把配置工具称为自动激活实现。

VS Build Tools 2022 C++/SDK 已安装；同一 canonical carrier 源码的 MSVC
与 LLVM 产物现已具备，先前“本机缺少 MSVC”的记录是历史限制。
编译器差异不等于已定位游戏 fault；这也不代表 Feeder 或游戏/NVIDIA
Feature18 已验收。#151 没有鸣潮画面验收，黑线仍需游戏确认。

以下保留已发布基线与历史交接，具体新候选以本节及 #80 最新评论为准。

更新批次：2026-09-07。此页同时供本地 Codex、ChatGPT App 和云端对话读取。旧对话中的 SHA、旧路线表和“已安排”不作为完成证据。

以下已发布批次与历史验证是基线；本轮尚未合入、尚未打包的修改及实际验证边界见下方“2026-09-07 定点接续”。不要把基线 Windows/游戏结果套到新 PR。

## 当前主线收敛批次

维护者于2026-09-07明确授权合并最新版本到 `main` 并改善反馈/稳定性。当前开发入口改为 `main`，本轮候选 `0.4.5-DX11-兼容增强`、PE `0.4.5.104`。dev3的fix2、DX11、RR/缩放后置历史和main独有52份反馈文件均保留。收敛分支清单与本轮验证见 [MAIN_CONSOLIDATION](MAIN_CONSOLIDATION.md)，玩家说明见 [0.4.5-DX11-兼容增强更新](RELEASE_DEV4_PLAYER_NOTES.md)。核心源码固定dccb5b3，完整Windows/WARP已通过，精简中文OTA由c7c133e打包，实际PE0.4.5.104；完整哈希/日志见MAIN_CONSOLIDATION。下面dev3发布/实机证据保留为基线记录，不代表新包实机通过。

当前[中文精简OTA及配套安装器发布页](https://github.com/smartLanny/dlss5-nr-before-sr-lab/releases/tag/beta0.4.5-dx11-compat)。OTA仅7文件、单中文核心；机器源码/打包/PE身份与哈希见MAIN_CONSOLIDATION。main已实际整合，33个临时远端分支已安全清理；0.5/Feeder/Manager独立未完成工作仍保留。Manager既有分支已推进7e2d16e，0.4.5条目已替换为本包。

本机 `main` 检出位于原项目同级 `nr-main-unify`，已跟踪 `origin/main`；原product工作区及旧worktrees/未提交文件保留原样。后续从此main或最新origin/main建立短期工作分支，不在冻结旧检出继续积累修复。

## 已发布dev3基线来源

- 历史兼容集成分支：`codex/multiapi-045-integration`，PR #133，执行看板 #80。
- 本轮临时集成分支：`codex/045-unified-candidate-20260907`；合入后不作为第二条长期线。
- 候选：`beta0.4.5-multiapi-dev3`，核心 PE `0.4.5.103`，carrier `dlss5-native-carrier-multiapi-dev3.addon64`。
- [OTA预发布Release已发布](https://github.com/smartLanny/dlss5-nr-before-sr-lab/releases/tag/beta0.4.5-multiapi-dev3)，构建/Tag固定源码`b03f474ba5127ab057fbb5e93708c47eb9dbb866`。GitHub两个附件的上传状态、字节和SHA256已回查匹配；中文679998 bytes，英文667589 bytes。
- 精确源码提交以包内 `source-manifest.json` 的 `source_commit` 为准；本地门禁及产物哈希以 `validation.json`、`SHA256.json` 为准。文档提交可以晚于构建提交，不能用文档 HEAD 冒充已编译的代码。
- 维护线颜色修复 PR #137 的全部祖先及 fix2已被本组合普通合并；#137/#131等重复PR已于本轮关闭，见MAIN_CONSOLIDATION。#136 dev2 增量已在 `289a8e5` 实际合入。

## 本批实际内容

1. 颜色默认和推荐统一1.0，色彩/HDR放高级菜单，主页面可仅恢复模型强度1.0。保留用户显式设置、最终放大1.2及未知HDR保护。
2. DX11原生DLSS carrier与D3D12核心合用；保留活动R、独立subrect、输出偏移回填、原生回退和真实Reset观察。
3. D3D12真实Feature13成功输出可走RR→NR；用户Mode2请求保留，RR结束后普通SR仍用原请求。只接受已知硬件深度，线性/未知深度保留原生结果并报告原因。
4. 后置50/67/75/100%工作尺寸有真实O快照、面积降采样、NR工作面和匹配修改重建，NR比例不是游戏真实渲染比例。后置guide相位仍需游戏测试。
5. 颜色、运动及曝光FLOAT家族/TYPELESS使用合法typed视图和实际设备能力；不靠改GetDesc或吞访问异常放行。
6. F8保持单TXT，并补后置请求/计数与同次命令状态blend四值。未观察到或快照忙显示明确状态，不填默认值冒充实测。面板区分历史观察、NR成功、录制、提交和实际完成。
7. 面板按快速设置、处理方式和模型细节分组，主要控件使用独立标签、统一宽度和留白，长提示换行。保留固定状态区域、默认值和原有设置提交规则。
8. RR真实Release三入口退役Feature13 context；carrier去除NGX/设备创建访问异常转换，真实GPU完成后才回填，超时保留依赖并回退原生。DX11同步可能影响性能，不以CPU Signal伪造完成。见[审查收口](DEV3_REVIEW_DISPOSITION.md)。

## 本批范围与边界

本测试包面向D3D12原生DLSS前/后置、已知硬件深度RR后置，以及DX11已有原生/模组DLSS carrier路线。BG3旧DX11版本的用户通过记录保留，新组合的DX11尚未实测；2077已有以下有限基础记录，不能推广至其他游戏。

Feeder/provider-only旧草稿已恢复并硬化，独立分支`codex/045-feeder-recovery`，最新源码`f6dbc3dce6bd33a656b0c3e377142262a1302302`。共享core/consumer DLL及4导出、46准入/23 Windows producer断言通过；用真实官方固定SDK编译完整Feeder/MinHook/资源通过，但最终链接需要本机缺失的MSVC C++运行库/ABI。完整同帧NVIDIA执行及Signal/Drain故障/设备重建尚未验收，因此不加入此候选包、不宣称支持无DLSS游戏。详细记录在该分支`docs/interop/FEEDER_RECOVERY_20260907.md`；不要重新从旧未提交草稿开始。

原生Vulkan同帧传输、DX9/DX10/OpenGL/x86 Legacy、线性深度/FFX RR、任意后置输出格式和通用引擎真实R控制仍未完成。MFG、新FG、N>R与0.5算法不在本批。

异环#53、低分后置#82、NBA2K25#127等游戏Issue不因构建通过自动关闭。异环原日志已有SR成功后第二次状态拒绝和开场交替，但没有实际blend四值；本批新增取证，未以删保护代替修复。

## 验证与交接状态

本批已完成本地构建、基础实机测试、两个OTA发布及线上回查。普通玩家说明见[更新说明](RELEASE_DEV3_PLAYER_NOTES.md)，精确日志/哈希与CI失败边界见[本地验证](DEV3_LOCAL_VALIDATION.md)，复现命令见[Windows验证](VALIDATE_DEV3_WINDOWS.md)。现在交由维护者详细测试，等待新的开发指示。

本机5.6-sol medium通过实际computer-use操作2077，直接启动EXE、不经Steam。346a74f完成普通SR存档场景F6/前置100%/后置100与75%，以及RR主菜单后置和两次F8单TXT；最终b03f474仅纠正RR日志名称，完整Windows门禁重新通过，再做RR开启/关闭两次独立主菜单短复测。实际1024×768、SDR、FG关闭。RR存档场景、4K/HDR/FG/性能/长时与其他游戏仍未验收；[详细分层游戏记录](CP2077_DEV3_SMOKE.md)。

原游戏组件、配置和原日志9项SHA256已恢复，父任务独立重新计算通过；游戏进程为0，候选组件已移出游戏目录。游戏自动新增的测试AutoSave-2可逆归档，未覆盖既有存档。本地原始游戏证据和全部构建失败批次保留，不上传原日志/游戏资源到源码仓库。

GitHub Actions目前受账户付款/额度限制，任务未启动；本地执行对应源码/Windows/Shader/WARP/反馈/打包门禁，明确记录为本地。WARP的神经输入采用确定性测试替身，仅验证生产GPU处理与回填，不等于NVIDIA Feature18或游戏运行。

## 2026-09-07 定点接续（未合入main）

入口仍是本页与 #80。接续基线 `origin/main@db0577c565d0a81a6f5c010b35698e078adf1205`，已含 #64 发布后 dx11-compat 原 F8。维护者权重是 **#99+#15、#153、#64+#38（#154）、#152（#156）、#39（#151）**；#149+#150 已有诊断，#155 仅文件名；#121/#148/#139/#140 有余力再看。未扫全 0.4，#141–#147/#127/异环混线 #53/#97/#100/#101 不并入本轮验收。

### 已完成的实际补丁：PR #154

沿用 `cursor/nullguard-finite-blend-e1c3`，保留原提交 `7eed541c` 与最新 main 的祖先。**代码/测试提交 `23f9bba67e7a3ccadbbaa9624d513779828e4617`**；后续仅文档变动不能冒充新的二进制来源。接续分支 `fix/priority-finite-blend-windows-20260907` 为同组代码保留，不另开重复 PR。

- #99 树海 multiapi-dev3 原 TXT 已通过 ROG 读取，#64 NeroEggs 已发布 dx11-compat 原 F8 已通过 GitHub 读取：两份独立样本都出现 `blend=-0,-1,-2,-3`、`dynamic=0x8`、covered/freshReset、无 taint/PSO，却被 null-guard 旁路；不再拿 #101 代替本单证据。#38 旧 fix1 没有四值，仍是同族假设，不能转用其它游戏的实测结论。
- 已保留有限 blend 原值准入/回放，NaN/Inf 拒绝；补齐生成器 STATE_CURRENT 锁定到实际生产 blob `47598e4068e1ba37327f22a309b17d6b2f0279e2`，未删除 source/dirty/祖先门禁。扩展全部通道/极值/位保持回归与三组成对 WARP 像素测试。
- Linux生产准入测试已有GCC/Clang O0/O2每组172项及旧规则/Inf/Reset/taint负控。本次ROG对真实HEAD `4521edf8aa9ddb6b20077d7f0debc789669c0779` 完成Windows转发38/准入172、native66/默认null718/栈197、24场景WARP及新增3组有限blend成对像素、坏blend负控；完整staging/双语核心/nrchain/carrier编译、48场景scaled-work WARP和相关全量门禁均通过。精确产物哈希与命令见 [NULL_GUARD_BLEND_FINITE_20260907](NULL_GUARD_BLEND_FINITE_20260907.md)。
- #99 RR 累计 attempts=4191/recordings=0 同时 recentAttempt=0、attemptAgeMs=39203；不能把当前 mode=1 快照当活跃 RR 终态。#15 全链路与 #150 边界计数仍独立验收。启动 installing/native-hooks-unavailable 不当最终失败。

### Windows / CI 边界

ROG连接阻断已解除；本次直接在Windows完成上述验证，原product和其他worktrees保持原样。源码manifest与validation均固定 `4521edf8aa9ddb6b20077d7f0debc789669c0779`，后续文档HEAD不是新的二进制来源。Windows compile与WARP通过；NVIDIA Feature18、真实游戏和长期测试仍未执行。没有发布新Release。

代码23f9bba对应 [CI run34136127356](https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/34136127356) 的实际annotation现已读取，确认付款/额度导致任务未启动，steps=[]。本地通过不标成GitHub CI通过，未盲目重跑。

### 其余优先项保留

#153 已核对固定上游 `NIGos/dlss5-bridge@28aed409` 的 DllMain/StartWatchingForNgx：尚未按实际呈现 API 判断时就进入 loader 选路、NGX 通知/Hook 注册及进程级异常过滤器等启动逻辑。下一步需要将轻量注册与有副作用的激活分离，处理辅助 device、晚载入/早期 Create、多 ReShade 与卸载生命周期；这只是可确认的源码缺口，不是崩溃根因或已实现修复。详见 #153 本轮回帖。#155 的配套身份匹配不能代替它。

#152 的 loader 候选仍在 #156；#39 黑线候选仍在 #151，不混 fix1 颜色正例。本次另由独立Windows任务验证这两组，以其PR实际回帖为准。游戏Issues保持open；#154的Windows/WARP阻断已收口，可进入合并审核。main、已发布Tag/Release/附件均未改。

## 下次对话如何接续

先读取最新远端 main、本页和 #80 最新回帖，再核对 PR #154 真实 HEAD 与包 manifest。本次已完成#154的Windows native/GPU正负控制和完整staging/build，不重复实现或把旧阻断当现状；下一步使用真正包含修复的新候选做 #64/#38 前→后、#99 普通 SR 与 RR 的同 scope 成功/录制/提交与 F6 对照。**旧 A 包、旧 dev3、已发布 dx11-compat 只能提供基线，不能验证新增代码。**

继续按上方权重推进，不重新恢复已提交文件、不覆盖任何旧 agent worktree、不把不存在的 `8a0a2d73` 当来源。持续旁路必须说明原生链路保留，并指引新候选的 F8 单 TXT 上传 BUG；已有旧资料不重复索要。视觉异常方便时使用 F7 对照。
