# Manager dev.9：真实游戏配置误拦启动修复

Manager **0.4.6-hotfix.7-dev.9** 接续 dev.8 和 #195。dev.8 的单独临时 EXE 验收没有覆盖 NVIDIA 已存在的官方共享游戏配置；用户提供死亡搁浅导演剪辑版和博德之门 3 后，在本机实际配置中复现了遗漏。本次不把前一轮单测或无 profile 探针当成真实游戏配置已通过。

## 原因与修复

1. 旧 `nvapi-drs.ps1` 把 `SETTING_NOT_FOUND` 写成 -165，正确值是 **-160**；-165 是 profile 名称为空。DS/BG3 实际已有 profile 中没有模型覆盖项，旧代码误报读取失败。现在缺省返回 absent/null，明确零值、继承值和真实失败分开；真正 -165/-175 不吞掉。依据 [NVIDIA 定义](https://github.com/NVIDIA/nvapi/blob/main/nvapi_lite_common.h#L317)。
2. 旧模型服务对没有任何保存策略的游戏，也执行隐含 auto 写入。现在未设置游戏只显示建议，打开超分面板不创建覆盖；启动走原样路线，不调用不必要的 NVIDIA 配置检查。已显式保存的旧策略和恢复记录继续处理。
3. 新适配器原本拒绝共享 profile，但服务先写 pending，之后又只接受独占 profile 的恢复快照，导致自锁。现在写前检查可写范围；兼容 dev.7/8 已拒绝的旧记录时，仅在原四字段配置身份和全部八项值都没有变化时清理未写入记录。不同状态保留证据，不能一概删除。
4. 用户明确选择“按游戏官方配置应用，并明确提示影响范围”。官方预定义游戏 profile 现在支持受控八项 SR/FG 参数写入；全部应用关联规则形成指纹，写前比较和读回都必须吻合。保留所有关联和其它设置；不修改全局或第三方自建共享 profile，不搬移 EXE、不删除官方 profile。

DS 官方配置关联 4 个入口，BG3 关联 6 个。UI 在超分补帧区域显示配置名、关联数量，可展开查看入口；这不是再次确认流程。独立 EXE 不增加共享提示。范围只读失败不会阻止原样启动。

## 模型与 API

SR 默认选项为“自动推荐”：明确 RTX30 使用 K，RTX40/50 使用 M（推荐）；请求保存 `preset:auto`，应用时按当前已确认硬件解析，未知/混合显卡不猜测。L 标“高画质低性能”。手动 K/L/M 保留。没有实际旧策略时不显示为“沿用旧模型”。

主页下增加两行“用爱发电”“不要付费”。API 自动项之后依次为 DX12、DX11、Vulkan、DX10、DX9、OpenGL。

实测 007 First Light 的 EXE 导入 d3d12.dll，但导入的 AMD AGS 同时含 D3D11/12 能力，被误计为游戏自身的混合 API。通用显卡 SDK 不再作为游戏引擎路线；实际引擎冲突仍保留。依据 [AMD AGS SDK](https://github.com/GPUOpen-LibrariesAndSDKs/AGS_SDK)。本机同一 EXE 修前 mixed，修后 dx12，已保存的手动 API 不覆盖。

BG3 加入按 Steam AppID 1086940 和精确相对路径绑定的官方入口规则：`bin/bg3.exe` 是 Vulkan，`bin/bg3_dx11.exe` 是 DX11。不会仅凭同名 EXE 套用，也不会把手动 API 选择当作切换 EXE。对已保存的相反选择保留提示，未擅自重绑或改装用户游戏。依据 [Larian 支持说明](https://forums.larian.com/ubbthreads.php?Number=818949&ubb=showthreaded)。当前 Vulkan NR 安装路线仍未开放。

## 实际验证

- 两游戏旧 pending 均已按原状态恢复，前后八项驱动值及配置身份相同，pending 清空；原记录备份留在 `build/dev9-recovered-shared-profiles`。
- 对两游戏的官方配置实际执行 SR balanced / 自动 M、FG fixed 4×，共八次写入/恢复调用；配置读回和应用关联指纹通过。随后 FG→SR 恢复，全部八项及 profile/入口关联与写前完全相同，无残留 pending；临时收据也恢复原状。
- 证据 `build/dev9-official-profile-live-c4ff89fa-e86e-47f2-be23-dd9c8c9add7f/result.json`。没有运行游戏或 GPU 工作负载，`runtimeVerified:false`；不能宣称游戏画质、实际倍率或 NR 兼容性通过。
- 实际 C# 接口模拟覆盖缺省/零值/继承/错误码、官方共享写入恢复、其它设置保留、关联指纹变化、全局/自建共享拒绝、零原关联创建/删除。新旧 profile 快照边界保留。
- 完整本地回归 **472 passed / 0 failed / 0 skipped**，UI 合同 74/70；实际包内 renderer 126 项隔离交互和 14 项游戏卡动作检查通过。覆盖初始化无写入、自动应用、范围提示、改名、安装忙状态和滚动位置。

本候选继续保留 dev.8 的管理员外壳、早期启动诊断、精简独立诊断、历史版本校验及恢复改进。核心 #206 dev12 与继承它的 #207 已在异环/2077 按 `=` 打开菜单时发生崩溃/卡死，不能发布或并入默认包。核心任务已恢复 dev11 并继续 dev14 排查；Manager 不改动其游戏目录。公开联合发布继续等待核心正式交接和实际验收，#204 carrier 也未作为已通过组件混入。


## dev.8 新反馈的接续修复

- 异环旧 HT 条目把 `HT/Binaries/Win64` 当作整个游戏根，漏查同一 WindowsNoEditor 下 Engine 的普通 DLSS。现在从真实 EXE 沿有界引擎布局核对原安装记录和 DLSS 所在目录；同 EXE 的根别名收敛，独立安装收据仍保留恢复入口。添加预览与最终列表共用 API 证据；没有绑定本次启动的历史 ReShade 设备日志仅作说明，不覆盖真实 EXE 的 DX12 证据。手动 API 仍按 EXE 保存。
- 导入核心的原来源丢失时，按安装收据继续检查已部署文件，并单独说明重新导入或显式选择可用版本。不会悄悄换成默认核心；卸载恢复不依赖原导入包。文件若已被其它任务/工具替换，继续要求核对所有权，不能声称所有手工 DLL 已自动迁移。
- 移除游戏先恢复 SR/FG 和本工具组件，再按 NR 收据卸载、恢复备份，成功后移出列表；失败保留条目。遮罩固定在所有 sticky 标签上方，不能再被“超分补帧”挡住。
- 普通 DLSS 未检出时，超分补帧仍可查看原因和恢复旧设置，但不允许新建覆盖或准备兼容组件；主进程同样校验。不会要求用户必须先运行一次游戏来掩盖缺省驱动设置的读取 bug。
- 已安装核心切换保留显式“应用版本”；下拉改变后提示尚未应用。丢失的导入版本显示为当前不可用来源，不能把默认下拉值伪装成已装版本。未安装游戏使用下拉版本执行下一次一键安装。
- 图标读取直接解析 EXE 的大尺寸资源，异环实取 256×256，失败再用系统图标。只替换与旧系统提取图标逐字节相同的自动图标，保留用户自行选的图标；缓存有数量和文件身份边界。旧 Steam 模糊缓存改用尺寸校验的新缓存。
- `CrashReportClient.exe` 仅在可证明是独立 Engine 子目录、与部署目录分开且无插件文件/链接时排除。选定 EXE、本目录 Reporter、无法确认的情况仍沿用进程保护。截图旧 PID 已不存在，没有宣称复现该 PID，也没有终止进程。

## 稳定兼容核心的真实来源

用户给出的两个路径分别是 `beta0.3.3.6-small-9b10` 的轻量更新 ZIP，以及 `beta0.3.3-dev-r4-2869` 原包。后者 DLL 的真实 PE 版本为 **0.3.3.4 beta devr4**，来源提交 `9085a4d67e32b8a6c83bc184b80ebe26b59c207d`。目录与清单保留真实 ID **0.3.3-dev-r4**，界面显示 **0.3.3-r4 · 稳定兼容**，不会把 0.3.3.6 改名冒充它。

r4 中文核心 SHA256：`2869d7ee12bcb73c4fadc4e2673a7bf3290fd2d90a294b2437e64b72c1bdd9e8`。仅按验证过的原件增添核心及 INI；原来的固定组件和其它核心字节不变，不为 r4 杜撰 carrier 支持。保留 0.2.0、0.3.3-r4、0.3.3.5（历史对照）、0.4.2 Beta、0.4.6-hotfix.1 Beta。Manager dev.9 和这些内置 Core 版本分别标注。

### 新反馈游戏的只读 DRS 检查

`build/dev9-reported-games-readonly.json` 记录巫师3、终末地和异环真实 EXE 的旧 SR 适配器和新八项配置适配器读取结果，全部成功。没有设置覆盖项时正确返回 absent/null；没有启动游戏、写入驱动配置或改动三游戏的插件文件。

## 最终交付与现用安装

完整 Setup / portable 已构建，Setup 在现用 `C:/Users/pc1/AppData/Local/Programs/xiaofeng-dlss5-manager` 安装返回0；已安装 EXE、ASAR、两份 NVIDIA helper 和独立诊断与构建内容一致。Manager重新启动，日志确认dev.9、管理员权限、窗口可见、renderer ready，原dev.8正常退出。没有要求 `--no-sandbox`。

实际ASAR使用隔离数据通过126项交互与14项游戏卡操作，主代理复核1280×900/900×620含实际主页/交流群链接的布局。所有src文件与ASAR逐字节一致；五个核心及FG组件/许可证校验通过；portable与内部EXE的真实manifest均为requireAdministrator。external unpacked不含NR payload且保留FG/NVAPI/诊断文件。

真实已安装EXE的独立诊断导出返回0，报告4026字节；程序主窗口仍保持打开。诊断ZIP含“启动诊断.cmd”、脚本和使用说明；不能打开Electron时可单独运行。

| 成品 | SHA256 |
| --- | --- |
| dist/win-unpacked/resources/app.asar | `c1122352fd70ed04884a8cb00845b474e3e1a1c9ca7df2227fcaff8f4ae6ca23` |
| dist/win-unpacked/DLSS 5 AI 超分管理器.exe | `1a9f0960e338f651dd129cbf90f1b272506466ec5a2ff22d98d0775a7239b6eb` |
| dist/DLSS5-Manager-Setup-0.4.6-hotfix.7-dev.9.exe | `4e45e27642f5f42ed79a15c5956ab1ae5ba29b5dab62acd3b4e639c3bd90de78` |
| dist/DLSS5-Manager-0.4.6-hotfix.7-dev.9-portable.exe | `d67ad980b29413926b066938fbf5da7719aa60f8506f5e6dc7a059c2c64a58a5` |

本地证据：`build/dev9-installed-file-verification.json`、`build/dev9-installed-startup.log`、`build/dev9-admin-manifests.log`、`build/dev9-diagnostic-verification.json`、`build/dev9-asar-ui.log`、`build/dev9-asar-card-actions.log`、`build/dev9-all-final-tests.log`。原dev.8 Setup保留，更新前ASAR/摘要备份在`build/installed-before-dev9`。全部验证均有实际边界；未据此扩大游戏兼容性声明。
