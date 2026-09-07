# 统一开发与测试交接入口

更新批次：2026-09-07。此页同时供本地 Codex、ChatGPT App 和云端对话读取。旧对话中的 SHA、旧路线表和“已安排”不作为完成证据。

以下已发布批次与历史验证是基线；本轮尚未合入、尚未打包的修改及实际验证边界见下方“2026-09-07 定点接续”。不要把基线 Windows/游戏结果套到新 PR。

## 2026-09-07 优先修复组合验证入口

当前可复现入口是临时分支 `codex/priority-validation-stack-20260907`，
基于 `origin/main@db0577c565d0a81a6f5c010b35698e078adf1205`，正常合并并保留
PR #154、#151、#156、#155、#157、#149、#150 的祖先。**main、Tag、Release
均未改变。** 精确合并顺序、冲突处理、命令、manifest、产物和日志哈希见
[PRIORITY_VALIDATION_STACK_20260907](PRIORITY_VALIDATION_STACK_20260907.md)。

完整 Windows canonical build 与本地 package 固定来源
`a2a338012a97f98f9327c141f32e274989602eb1`。生产 core Git blob 为
`52a2782b29e8a9f43cec2c03cf69ec01143cd6d8`，command-state blob 为
`f0f13b0a8ac243f01cdf0f93a517440ac65f1394`。生成器与测试身份门禁已重新
固定到组合源码，没有删除 dirty/祖先/source 检查。

组合源码实际通过新增 focused 回归、完整 staging、6 个生产 HLSL 入口、
runtime-load 真实 DLL fixture、RR 边界 50-case、LightingLock 生产
`CombineV2CS` WARP、finite blend 原生/恢复 WARP、48 场景后置 WARP、
静态分析、双语 core/nrchain/carrier 编译与 canonical package。这里的 WARP
神经输入是确定性测试替身；`game_runtime_verified=false`，NVIDIA Feature18、
鸣潮黑线、#99/#15 RR、#64/#38 后置以及 DX11 游戏均未由此验收。

#157 只提供配置关闭时不启动 carrier 监听/Hook 的最小 source-off 路径；真实
DLL 探针已证明关闭配置时 device 创建从 1 降为 0。自动 API 激活、Manager
生命周期和 #153 崩溃根因仍等待维护者选择架构，不在本组合继续实现。
canonical 包仍含可能触发 #153 的 carrier，因此不是通用 OTA：D3D12 游戏按
现有手动安装规则不安装 carrier，本地组合包不自动安装、不发布。旧 r3 carrier
实际由 MSVC `cl /O2 /MT` 构建，本组合 carrier 使用 LLVM-MinGW；ROG1 没有
可用 `vswhere`/`cl`，同源码工具链 A/B 尚未执行，这是 #153 保留变量。

GitHub hosted CI 仍受账户付款/额度限制，实际 job `steps=[]`；这是 CI 未启动，
不是绿灯或代码测试失败，也未盲目重跑。各 PR 当前可审查/无合并冲突的状态
不能替代 main rules；账户 plan 对 rules API 返回 403，因此不声称特定保护规则
已经放行。

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

先读取最新远端 main、本页、组合验证报告和 #80 最新回帖，再核对临时分支
真实 HEAD、包内 `source-manifest.json` / `validation.json` 与外层 handoff。
构建来源固定 `a2a338012a97f98f9327c141f32e274989602eb1`；后续只合入的 PR 文档与
本交接文档不是新的二进制来源。不要因文件名和 PE 显示版本相同而把已发布
dx11-compat 包当成该组合。

下一步由维护者决定 #153 的激活架构；未授权前不进入自动 API 激活或 Manager
实现。若只做游戏验收，D3D12 不安装 carrier；分别复测 #64/#38 前→后、#99
普通 SR 与 RR、#39 LightingLock/HDR，并记录同 scope 成功/录制/提交、F6 与
F8。持续旁路必须保留原生链路并提示反馈。#148 的 plain045 表单与 exp1/feed-only
附件、#139/#140 的 exp1/null-PSO 或 Feeder+DFC 证据保持分桶，不用于关闭本批
Issue；#121 不继续旧版本 bisect。不要覆盖旧 worktree，不自动 merge main、安装
游戏、发布 Tag/Release 或替换已发布附件。
