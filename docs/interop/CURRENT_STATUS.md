# 统一开发与测试交接入口

更新批次：2026-09-07。此页同时供本地 Codex、ChatGPT App 和云端对话读取。旧对话中的 SHA、旧路线表和“已安排”不作为完成证据。

## 唯一来源

- 持续兼容集成分支：`codex/multiapi-045-integration`，PR #133，执行看板 #80。
- 本轮临时集成分支：`codex/045-unified-candidate-20260907`；合入后不作为第二条长期线。
- 候选：`beta0.4.5-multiapi-dev3`，核心 PE `0.4.5.103`，carrier `dlss5-native-carrier-multiapi-dev3.addon64`。
- 精确源码提交以包内 `source-manifest.json` 的 `source_commit` 为准；本地门禁及产物哈希以 `validation.json`、`SHA256.json` 为准。文档提交可以晚于构建提交，不能用文档 HEAD 冒充已编译的代码。
- 维护线颜色修复仍有独立 PR #137，本组合普通合并其全部祖先及 fix2。#136 dev2 增量已在 `289a8e5` 实际合入。

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

本测试包面向D3D12原生DLSS前/后置、已知硬件深度RR后置，以及DX11已有原生/模组DLSS carrier路线。BG3旧DX11版本的用户通过记录保留，但新组合未经游戏验证。

Feeder/provider-only旧草稿已恢复并硬化，独立分支`codex/045-feeder-recovery`，最新源码`f6dbc3dce6bd33a656b0c3e377142262a1302302`。共享core/consumer DLL及4导出、46准入/23 Windows producer断言通过；用真实官方固定SDK编译完整Feeder/MinHook/资源通过，但最终链接需要本机缺失的MSVC C++运行库/ABI。完整同帧NVIDIA执行及Signal/Drain故障/设备重建尚未验收，因此不加入此候选包、不宣称支持无DLSS游戏。详细记录在该分支`docs/interop/FEEDER_RECOVERY_20260907.md`；不要重新从旧未提交草稿开始。

原生Vulkan同帧传输、DX9/DX10/OpenGL/x86 Legacy、线性深度/FFX RR、任意后置输出格式和通用引擎真实R控制仍未完成。MFG、新FG、N>R与0.5算法不在本批。

异环#53、低分后置#82、NBA2K25#127等游戏Issue不因构建通过自动关闭。异环原日志已有SR成功后第二次状态拒绝和开场交替，但没有实际blend四值；本批新增取证，未以删保护代替修复。

## 验证与交接状态

维护者最新授权发布一版新OTA预发布Release，并在本机用2077做基础测试；测试必须具有可用computer-use能力，直接启动游戏bin/x64，不经过Steam。完成后交由维护者详细测试，等待后续开发指示。发布/游戏测试结果以#133/#80和Release实际回查记录为准，不能把“已安排”算完成。普通玩家说明见[更新说明](RELEASE_DEV3_PLAYER_NOTES.md)，复现门禁见[Windows验证](VALIDATE_DEV3_WINDOWS.md)。

GitHub Actions目前受账户付款/额度限制，任务未启动；本地执行对应源码/Windows/Shader/WARP/反馈/打包门禁，明确记录为本地。WARP的神经输入采用确定性测试替身，仅验证生产GPU处理与回填，不等于NVIDIA Feature18或游戏运行。

## 下次对话如何接续

先读取远端#133及本页，再核对包manifest和最新Issue回帖。维护者本轮要求交付测试版后等待进一步开发指示；收到新指示时，从已记录缺口接续，不重新恢复已提交文件，不覆盖任何旧agent worktree，不把不存在的`8a0a2d73`当来源。

推荐回传：原场景F6、前后置效果、一个F8 TXT；视觉异常方便时F7。已有资料不要求重复提交。
