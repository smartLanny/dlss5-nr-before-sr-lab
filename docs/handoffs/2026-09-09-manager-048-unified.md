# 管理器 0.4.8-beta.1：统一详情、应用与启动

本次在独立工作树 `manager-048-unified-20260909` 实施，基于 beta.2 的 `e2fdf03665c70e95638b172ae06496974495cf4d`。未覆盖旧管理器，未对用户真实游戏目录、驱动配置或线上 issue 执行写入。

## 用户流程

- 游戏库卡片进入独立详情页，三个页签集中提供 API 对照、增强编辑、维护与备份。
- API、完整 Core 配套、外置目录、加载方式、NR、SR、FG 和面板快捷键先暂存，在同一次操作预览中核对并应用。游戏开关确认只保存与 EXE 身份绑定的用户声明。
- 不支持的模式与倍率保留置灰；驱动读回一致仍标为已应用、需重启、实际效果待确认。NVIDIA Recommended 按 DLSS 档位请求 K/M/L，未知游戏档位不伪造推荐值。
- Steam 游戏自动优先 Steam，启动后继续匹配所选 EXE 的完整路径和进程创建时间。超时、取消、不同 EXE 或助手失败不会偷偷切换直接启动。
- 每次卸载分别选择干净移除或恢复安装前。清理预览只读，清理隔离按明确选中的文件执行并保留原件；HDR、其他模组和来源不明 Add-on 默认不选中。卸载和设置恢复完成后可单独移出游戏库。

## 接口与恢复边界

`GameAssessment` 聚合 API 能力、当前配置、观察结果、桥接、驱动资格、组件身份和四层验收。组件检查区分固定摘要、PE/内容自声明和文件名线索，读取范围与资源量有界。

`OperationPlan` 保存明确的执行阶段和目标/配置/收据指纹。应用前重新编译并核对预览，每阶段先写入意图。已经完成的阶段不会被描述为全部回滚；未完成事务留在原所有者处恢复，恢复入口处理外置、Vulkan、Feeder、FG 和设置各自的记录。

`LaunchSession` 绑定本次目标、启动请求和助手生命周期。加载助手使用本项目普通权限的 Windows 原生程序，只为受管 ReShade 与精确目标游戏加载；Ready 通过私有输出协议核对会话、EXE 和配置摘要。失败只结束本次自有助手，不结束游戏、不修改系统保护、不删用户插件或组件库。

DX11/DX12 外置目录包含活动 ReShade.ini、Core、依赖、参数和日志。迁移保留用户滤镜/纹理/预设路径原有语义。Vulkan 和 Feeder 保持固定配套与各自部署所有权。

主界面保持普通权限，沙箱、上下文隔离默认开启。需要管理权限时，用户在预览中明确执行一次性操作；受限 worker 核对父进程、应用代码身份、nonce、计划摘要、EXE 和一次性结果，不创建第二个业务窗口。Electron 固定为 44.3.0。

## 四层验收

1. 助手就绪：本会话有效 Ready；无需助手时为不适用。
2. 本 Core 已加载：目标进程同时加载受管 ReShade 和指定摘要的本 Core。
3. NR 成功：本 Core 日志新增的成功与提交计数持续增长；保存首次旁路原因。`not-attempted` 不推断为缺 DLL，格式 27 被拒绝时报告格式旁路。
4. 画面变化：绑定会话、EXE、进程身份和 Core 的用户同场景对照记录，可附 F8/ColorDiag 材料名称。来源为用户观察，不冒充自动画质验证。

旧日志、历史会话、换 EXE、换 Core、HDR 成功不会让当前 NR 验收自动通过。TYPELESS 格式实现与新 Core 的实际游戏验证由独立 Add-on 线继续推进。

## 固定配套与参考依据

- [#224 BG3 DX11 桥接回归](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/224)：对照项 `0.4.7beta-bg3-bridge1411` 保持 Core、nrchain、配置与 ReShade 字节不变，只换用户提供的 Bridge 1.4.11。旧桥 SHA256 `73d438ee9427e73d9919d169a107c7f5d73f60b291ea2cd66bd33279a35d3e95`，默认新桥 SHA256 `4656d9aac382a6f9b5c8488669aa5365283f03b5b94b2267f1c7f9b53927dc86`。默认版本仍为原配套；社区进入游戏报告与本机 NR 验收分开。
- [#191 外置加载](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/191)、[#2 方案表](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/2#issuecomment-5602949565)：参考 HoYoShade Beta9 与 GI Ready 的路径记忆、外置 payload、插件允许清单及 Ready 流程。没有复制其覆盖个人配置或缺失生命周期清理的行为。
- [#45 HDR 共存](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/45)：区分 HDR、Generic NR 和本 Core；调整面板键保留 HDR 与滤镜参数。
- [NVIDIA 覆盖设置说明](https://nvidia.custhelp.com/app/answers/detail/a_id/5620/)与[DLSS 4.5 官方说明](https://www.nvidia.com/en-us/geforce/news/nvidia-app-dlss-4-5-dynamic-multi-frame-generation-available-now/)：游戏内开关、驱动能力、具体游戏和请求档位分别作为资格依据，枚举值不代表任意游戏支持。
- [Steam RestartAppIfNecessary](https://partner.steamgames.com/doc/api/steam_api#SteamAPI_RestartAppIfNecessary)：经 Steam 启动可能产生不同 EXE，必须等待并匹配目标游戏。
- [Electron 官方发布记录](https://releases.electronjs.org/)：44.3.0，2026-09-08。默认沙箱遵循[官方进程沙箱说明](https://www.electronjs.org/docs/latest/tutorial/sandbox)。

## 验证范围

集成验证使用临时游戏目录、真实 PE 测试程序、固定配套文件和受控驱动适配器。包含 DX11/DX12 普通→外置→升级→普通、代理/助手往返、NR/SR/FG 合并应用、RTX40 MFG 迁移与恢复、两类卸载、Vulkan/Feeder 重复预览及中途失败恢复。原生助手验证使用本任务自己编译的目标程序和模块。

真实 Electron 界面验证覆盖独立详情页、800ms 内无自动写入、统一应用、置灰资格、每次卸载选择、失败恢复、快捷键草稿、画面对照记录与移出游戏库。普通/软件渲染/临时兼容模式使用隔离配置和受控业务服务验证窗口、预加载和页面就绪。

以下结果必须保留为待实机验证：原故障电脑启动、真实 UAC 对受保护游戏目录的应用、安装版实际安装流程、BG3 1.4.11 桥接 NR/画面、米哈游 NR、RTX40/RTX50 的实际补帧与 NR 共存。测试程序与静态资源核验不能替代这些结果。

最终测试计数、冻结审查及交付文件核验记录随本次候选完成后补充。
