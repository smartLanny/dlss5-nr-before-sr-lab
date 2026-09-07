# 统一开发与测试交接入口

更新批次：2026-09-07。此页同时供本地 Codex、ChatGPT App 和云端对话读取。旧对话中的 SHA、旧路线表和“已安排”不作为完成证据。

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

## 下次对话如何接续

先读取远端main、本页和MAIN_CONSOLIDATION，再核对包manifest和最新Issue回帖。维护者本轮要求交付测试版后等待进一步开发指示；收到新指示时，从已记录缺口接续，不重新恢复已提交文件，不覆盖任何旧agent worktree，不把不存在的`8a0a2d73`当来源。

推荐回传：原场景F6、前后置效果、一个F8 TXT；视觉异常方便时F7。已有资料不要求重复提交。
