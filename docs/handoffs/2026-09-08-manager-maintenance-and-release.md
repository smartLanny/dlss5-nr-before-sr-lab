# 现用 xiaofeng 管理器维护与待发布交接

## 当前候选：Manager #180 性能旁路接入（dev.6）

当前本机为 **Manager 0.4.6-hotfix.7-dev.6**，保持 dev.5 用户确认的顶部 API/核心、皮肤常显、画面设置/超分补帧两页签布局。#180 以独立性能修改提交和验证，不与 #179 界面验收混同。

已核对 `b4d1d44` 的既有哈希/展示缓存、worker、封面索引及静默日志均在当前树，未找到更新但遗漏的可见性能旁路提交。本次另修复手动候选扫描无条件遍历普通 Content 素材树：只有根目录或直接 Content 目录的 MicrosoftGame.config 才启用 GDK 例外，显式选中的 Content EXE 保留。合成 9,600 素材的候选阶段由 9,601 文件回调/123 目录读取降为 1 文件回调/3 目录读取（含两次固定布局探测）。不是现场 IO 对照或普遍提速承诺。详细来源、25 项定向回归及 dev.5 本机短时进程观察见 [#180 独立验收记录](../performance/2026-09-08-manager-180-content-scan.md)。

最终完整 **293 passed / 0 failed / 0 skipped**，UI 合同 71/67，包内实际 ASAR 在 900×620 / 1280×900 下交互通过。实际包中 library-service 和 UI 文件与已验源码逐字节匹配；所有核心、FG、NVAPI 与许可证通过。portable/内部 EXE 保持 requireAdministrator；external unpacked 仍不含 NR payload，保留 FG/NVAPI。完整 Setup 更新本机返回 0，安装 EXE/ASAR 匹配后重新启动。安装前 dev.5 ASAR/摘要保存在 `build/installed-before-dev6`，dev.5 Setup 仍在 dist 可回退。

| 当前成品（`dist`） | SHA256 |
| --- | --- |
| `win-unpacked/resources/app.asar` | `c1dc5fc496c16e3b1dbc9924fd9819aab72ac4c8f09ec3317417263f1f06de35` |
| `win-unpacked/DLSS 5 AI 超分管理器.exe` | `7c4ab088e97c2e8281773bc44886ce90438d7cb495c129a4caa030d67a25fec1` |
| `DLSS5-Manager-Setup-0.4.6-hotfix.7-dev.6.exe` | `cce18a53d1c6d0f1bd0cdb83a348052e9ac525c0b5e4dda2a5efb709c2d9e20f` |
| `DLSS5-Manager-0.4.6-hotfix.7-dev.6-portable.exe` | `6f22479a9fc19c91c6afbd1548dcbd2e15b5dadc4adfefdb0550f53140e45721` |

Core 仍为 **0.4.6-hotfix.1 · Beta**，0.3.3.5 稳定基线保留；组件清单与此前安装相同，未引入未验收的 carrier。#180 原现场仍缺版本/发生阶段/CPU与Procmon采样，保持开放；兼容性支线正式运行时交付与公开发布继续等待，不能把本次本地 Manager 候选当成新核心公开 release。

## 最终界面选择与本机交付（dev.5）

用户在看到 dev.4 后明确选择：**API 和核心版本回到顶部公共区域，皮肤结构保护直接显示，只保留「画面设置 / 超分补帧」两页签；安装状态与画面设置同页**。已按该选择实现并更新现用安装，当前 Manager 为 **0.4.6-hotfix.7-dev.5**，下面 dev.4 的三页签描述为已替代历史。核心/桥接发布门槛和公开发布等待不变。

顶部 API/核心在两个页签均可访问。自动项及卡片显示「DX11 桥接（自动）」/「DX12（自动）」；可以指定 DX12，自动检测结果保留在自动项里，不覆盖手动选择。保存仍通过已有 EXE 绑定的路线服务，DX11 配套桥接、DX12 不部署 carrier/保留共用 nrchain 的规则不变。皮肤开关和保护强度无需展开；超分补帧页签保留延迟加载、草稿和独立保存/恢复。

最终完整 **292 passed / 0 failed / 0 skipped**，UI 合同 71/67。包内实际 ASAR 的 1280×900 和 900×620 交互通过，新增两页签循环、顶部公共控件、皮肤常显、DX11 自动/手动 DX12 独立展示回归；900×780 布局已检查。仍使用隔离 IPC/GPU 数据，未宣称游戏兼容性通过。完整 Setup/portable、external unpacked、实际载荷/FG/NVAPI 和管理员权限检查通过。Setup 本机更新返回 0，已安装 EXE/ASAR 摘要匹配并重新启动。`build/installed-before-dev5` 留有前版 ASAR/摘要，dev.4 完整 Setup 仍可回退。

| 当前成品（`dist`） | SHA256 |
| --- | --- |
| `win-unpacked/resources/app.asar` | `2681419b612a7e91f8108271c3f5e1f5f7e33b5beb2d954b0114817ea7f98740` |
| `win-unpacked/DLSS 5 AI 超分管理器.exe` | `05cf23572a278965019ae393d547e9267862452fbe0c5a9e17ccf58dc0cc53de` |
| `DLSS5-Manager-Setup-0.4.6-hotfix.7-dev.5.exe` | `384b2b729acb2a8e12b613f384f6856bcf92569999f3346d01f5d6660c0e959d` |
| `DLSS5-Manager-0.4.6-hotfix.7-dev.5-portable.exe` | `a9abe47b49bb13f87dd0952b9d14b9110fa044903ebccdb6530d92718fa91f0c` |

Core 仍是 0.4.6-hotfix.1 Beta，0.3.3.5 稳定基线不变；bundle.json 与此前安装版匹配。A 任务仍在推进真实游戏问题，未有新正式运行时验收交付，#194 仍为前述源码交付。没有把新 carrier 混入本候选，也没有公开上传此本地 UI 候选。

## 当前交付：整套工作区界面与本机更新（dev.4）

当前为 **Manager 0.4.6-hotfix.7-dev.4**，沿用 #195；以下 dev.3 / dev.2 及“尚未替换 hotfix.6”的描述均为历史记录。本次用户明确授权调整整个现用界面，已在独立维护 worktree 实现、构建完整 Setup/portable，并通过 Setup 更新到 `C:/Users/pc1/AppData/Local/Programs/xiaofeng-dlss5-manager`，重新启动成功。冻结测试 UI 未修改。没有公开上传本候选；等待兼容性支线后再发布的要求仍有效。

游戏详情改为「画面增强 / SR 与 FG / 安装诊断」，页签键盘可操作，切换保留草稿且不写入；SR/FG 第一次打开时才加载。常用 NR 强度前置，高级设置折叠并显示当前状态。组件管理集中来源、导入、可用版本及新装默认核心；修复页优先异常。额外修复来源切换后保留详情中的版本列表未刷新、旧异步诊断覆盖新选择的问题。范围及主代理视觉验收见 [设计验收记录](../design/2026-09-08-manager-workspace-ui.md)。

完整 **292 passed / 0 failed / 0 skipped**；UI 合同 71 IDs / 67 references。实际本次 ASAR renderer 的 1280×900 与最小 900×620 交互通过，另检查 900×780 多页面布局。数据/IPC/GPU 为隔离夹具，不是游戏验证。实际包内所有核心版本、FG 摘要、许可证、NVAPI 脚本及新 UI 文件验证通过；external unpacked 验证不含 NR payload，保留 FG/许可证/NVAPI。完整 portable 外壳、内部程序、已安装 EXE 的实际 RT_MANIFEST 全部为 requireAdministrator。

本机通过完整 Setup 安装返回 0，重启后的 Manager 进程存在；已安装 EXE/ASAR 摘要与本次构建一致。安装前 ASAR 与摘要留在 `build/installed-before-dev4`，原 hotfix.6 完整 Setup 仍在 `xiaofeng-sr-fg-20260908/dist` 可用于回退。用户设置及游戏目录未由本次维护直接修改。内置 `bundle.json` 与原已安装版本摘要相同：Core **0.4.6-hotfix.1 · Beta**、0.3.3.5 稳定基线、0.2 历史项，FG 仍为 RTX40 MFG v1.2 / UAL v9.7.4。新 carrier 源码交付没有混入默认组件。

| 成品（`dist`） | SHA256 |
| --- | --- |
| `win-unpacked/resources/app.asar` | `c84fda6f82cf183e2ffaccc28926692db3986decb0c0b974475ce68d8aefd4ca` |
| `win-unpacked/DLSS 5 AI 超分管理器.exe` | `4e0f7a9d5621dbc9ea60e606e4fcc2b2686588de4a252aced4208fcebadda7f1` |
| `DLSS5-Manager-Setup-0.4.6-hotfix.7-dev.4.exe` | `e039847837fb056e3938a83fe8b827dedb8728d97049e38ff8df24753a0d0350` |
| `DLSS5-Manager-0.4.6-hotfix.7-dev.4-portable.exe` | `5460e040e74a20c089ee8cda014de7d0e5f404a6028dc1a602b8e99a37a6fcbc` |

最新兼容性区分：#191/#195 的外部目录是安装来源，不等于游戏运行时外置 Addon；#171/#175 的外部 provider→Own Core 已有受控 RTX5090 Feature18/NR→SR 结果，真实游戏 feeder/guides/copy-back 等仍待验收；#194 的 DX11 carrier 是进程内 D3D11→D3D12 资源及生命周期桥接，不是独立进程隔离，真实游戏/NVIDIA Feature18/真实 ReShade 派发仍未验证。Native DX12 不部署 DX11 carrier；DX11 DLSS 最可能直接受益但不能先宣称通过。FSR/XeSS、TAA/TSR、Vulkan 仍分别依赖 provider/引擎输入/加载链路。#153 旧闪退没有据此关闭。

## 最新追加：运行库与 UI 检查（dev.3）

当前候选更新为 **Manager 0.4.6-hotfix.7-dev.3**，继续 #195；下面的 dev.2 / b8785be / 287 测试及构建摘要为上一批历史。当前安装版仍是 hotfix.6，Core 组件没有替换，兼容性支线与发布条件不变。

静态 normal/delay import 核对发现：现装 Manager/Electron 没有直接 MSVCP/VCRUNTIME 依赖，随包 ffmpeg/EGL/GLES/Vulkan 等文件存在；RTX40MFGCore.dll 和 RTX40MFG.asi 则确实依赖 MSVCP140/VCRUNTIME140/VCRUNTIME140_1。静态表不涵盖所有 LoadLibrary/驱动/NGX/Streamline 运行期加载，不能把用户“打不开”全部归因于 VC++。

已增加 RTX40 专属基础运行库检查，尊重游戏 EXE 目录优先、缺失回退 System32 的顺序，只读 PE 头确认实际 AMD64，拒绝把 ARM64 的 PE32+ 当 x64。缺库/无法确认时不再显示 FG 就绪，并提供固定微软官方下载与重检入口；本地 DLL 错误则提示验证游戏文件，不误导用户安装全局库就能替换本地坏文件。RTX50 不受此检查影响，恢复始终可用。不运行或静默安装任何运行库，不扫描 PATH，不加载用户 DLL。基础文件通过不代表最低版本、导出/依赖解析或游戏运行已经验证。

UI 检查另修正了默认核心版本提示仍暗示“点修复自动升级”的旧文案，以及 NR 保存提示容易被误解为 SR/FG 立即生效的问题。后续布局建议为：缩短游戏长页、常用设置/高级选项分层、突出每游戏组件身份和一项明确操作、将新装默认核心放回组件管理页。未实施未经用户选择的整套视觉重构。

最终完整 **292 passed / 0 failed / 0 skipped**（`build/runtime-final-tests.log`），实际新 ASAR renderer 在 900×1100 / 1280×900 下，缺库→官方下载→重检→允许准备及本地 DLL 错误提示交互通过，IPC/GPU 数据仍为夹具。真实 external unpacked 资源验证通过，内部 EXE manifest 为 requireAdministrator。新 `app.asar` SHA256 为 `7bdbd160967ccadc46e8403d4424f22da7c33faed6034b5abda6572bb94e83c5`；内部 EXE 为 `edc0e333b0a36e878ebf2ce268153fac9f63caad9dea2d6ca338dedf473e5a52`。尚未发布新 portable/Setup，不把本地目录当完整玩家包。

## 当前边界

Manager 候选 **0.4.6-hotfix.7-dev.2**，接续 #189 `063a428118996413cf157b914f5058b51e430438`，包含 #181 `94489177be989ab316fc9167ade41f9d75ce1a10`。维护分支为 `codex/manager-maintenance-20260908`。Core 组件基线仍为 **0.4.6-hotfix.1 · Beta**；0.3.3.5 稳定基线、0.2 历史项继续可选。候选没有替换已安装 hotfix.6，没有使用冻结的旧测试 UI，没有修改游戏目录。

用户授权先完善现用管理器、更新 Issues，等待兼容性支线维护完成后再打包发布。当前不把支线编译/夹具通过当作真实游戏验收，不覆盖旧 tag / release。公开源码不包含专有 NR runtime。

## 已实现

- 外部完整组件目录：兼容 bundle.json 根目录、nr-before-sr 的父目录或 payload 上层；校验通过才原子保存路径和清单摘要。启动恢复选择，失联保留游戏库，不静默换来源；清单变化需显式重新检查。
- 目录/版本/清单路径校验：限制大小、条目数量和版本标识，拒绝路径逃逸及符号链接/目录联接，操作时仍执行新鲜校验。切换来源不会改写游戏，也不抹去 EXE/API/目录记忆。新目录缺失已装版本时，修复拒绝静默升级。
- 最终复核实际复现并修复一项 P1：外部目录在 requirePayload 校验之后被替换，旧 installer 会接受变化后的字节。所有受管文件的复制前源摘要和复制后目标摘要现在均须匹配先前已验证摘要；包括首次配置、安装/修复与 OTA。确定性测试覆盖源变化、复制期间变化、旧文件/备份/收据回滚，以及首次配置失败后整笔安装撤回。
- 插件管理页先展示组件来源、导入更新、可用版本；历史说明折叠。移除过时的手动桥接复选框文案，按 API 自动部署。来源失败持续显示具体原因，游戏页有可用的“检查安装组件”入口；统一当前主题、图标和按钮层级。
- ReShade 禁用项仅清理 `[ADDON]` 中本工具核心的精确文件项，不误删相似名称、其他节、其他 Addon 或转义逗号。首次修改 ReShade.ini 保留原 `.bak`，诊断区分受管旧 carrier 与外部冲突。
- 错误保留原错误码、失败阶段和启动恢复信息；中文说明具体文件、原因和下一步。只有游戏已运行且属于运行时载入失败时才引导 F8；安装未开始不误导用户去游戏按键。
- 继承 #189 启动失败保留恢复记录、no-op 预览后检测外部改动；继续使用同一启动协调器与 journal，不新增互不知情的写入/恢复栈。
- 可选外部组件版构建：继承普通包配置，仅去掉 NR payload；保留 RTX40 MFG/UAL、许可证和 NVAPI 脚本，输出 `dist-external` 并标记 `external`。portable `admin` 与内部 EXE `requireAdministrator` 均保持。
- 保留 #180 的共享文件哈希去重、展示缓存、扫描 worker 与只读成功 IPC 静默日志；没有把更多全盘并发当作优化。

## 验证记录

最终 Node 回归 **287 passed / 0 failed / 0 skipped**，UI 合同 71 IDs / 67 references。本地日志为 `build/maintenance-final-tests.log`。生产载荷测试在公开源码无本地载荷时明确 skip；本次实际有经校验载荷，因此执行且通过，未计为 skip。

完整测试需使用当前 PowerShell 7 可执行路径，避免宿主 PowerShell 7 的 PSModulePath 被 Windows PowerShell 5 子进程错误继承；PR160 历史载荷工具可用时一并设置，不能把缺路径的 skip 算通过：

```powershell
$env:DLSS5_TEST_POWERSHELL=(Get-Command pwsh).Source
$env:PR160_CARRIER_TOOL='<reviewed PR160 source>/scripts/configure-carrier-exclusion.py'
npm test
node_modules/.bin/electron.cmd test/launch-settings-frontend.electron.cjs build/maintenance-900.png
```

900×1100 与 1280×900 的当前 Electron renderer 交互验证使用隔离 IPC/硬件夹具，覆盖来源选择、拒绝、断开、返回随程序组件、SR/FG 展开保持及既有设置应用/恢复流程。主代理已对比同尺寸前后截图并检查错误状态。截图和测试桩不是实际游戏或完整原生安装流程证明。

已构建外部版 unpacked 验证目录，检查实际 ASAR / resources 不含 NR payload，FG 文件、许可证与 NVAPI 仍齐全；实际内部 EXE 的 RT_MANIFEST 为 requireAdministrator。1280×900 隔离交互也加载本次实际 ASAR 内的 renderer 执行通过。已安装 hotfix.6 内部 EXE 与其 portable 外壳也重新核对该权限。本维护候选的 portable 成品仍须在最终发行时逐个核对 manifest，不把 build 配置测试代替成品验证。

本地外部版验收目录摘要：`app.asar` 为 `e91e121cc7671599c06d6ef937dedf6101af24f0e8ba588291ee8564495f0909`，内部 EXE 为 `e4b2b867ffc9f45b44bfa230a97bf45ab0de39ef1484c13a5756a628779d84b7`。现用装机宅图标保持 `53da9513c13e8af5914ed2975091b738b1f5820113b7760aabc66acee668fe3c`。它们是本次本地验收构建，不是已上传发行资产。

## 兼容性支线与发布门槛

A 任务“修复BUG”正式源码交接为 #194（基于 #185）：实际 DLL 源码 **213ae7a503fbab8a6b773368dfd55df77319ad1e**，文档交付 **3c136254e0d6c4f51f682050e76800abbcad891c**。后者不是 DLL 构建源。43 组门禁通过，139 项 Python 通过、2 项外部 Feeder 跳过；LLVM/MSVC、实际 DLL/MinHook 与 WARP 退役通过，最终三项 P1 已修正并定点复核。

交付清单明确 `game_runtime_verified=false`、`nvidia_feature18_verified=false`、`real_reshade_dispatch_verified=false`、`private_bridge_frame_verified=false`。它是源码/本地验证交付，尚未完成 A/M1 游戏里程碑，不能据此宣布 #153 原闪退修复。真实 DX11 输入、ReShade 派发和 NVIDIA/私有 NR 帧仍待支线推进。此阶段不把其二进制替换到默认玩家包。

后续恢复本任务时，先读取该任务的最新正式交付和 #194/后继 PR，不重跑或干扰其正在进行的工作。若没有新验收或发布交付则保持等待，避免重复留言。满足发布条件后：

1. 在本维护分支整合受影响接口/打包配方，核对源码、source-manifest、validation、中文核心、nrchain 与 carrier 的精确摘要。不能只替换 carrier 或沿用旧包元数据宣称新构建。
2. Manager/Core/载体构建身份分别记录；0.4 核心仍标 Beta，0.3 稳定基线保留。只带一份中文 Addon，精简无用附件，保留必要许可证和用户说明。
3. 用真实载荷验证安装/修复/OTA/切换/卸载与旧载体备份回退，复核 DX12 无 carrier、DX11 配套 carrier、共享 nrchain 不误删；重新跑受影响回归。
4. 构建 portable/Setup、可选外部组件版及匹配 OTA，验证真实 EXE manifest、ASAR、版本和 SHA256，发布新的验收 Beta，验证上传后的身份。不要覆盖既有 tag、伪造 stable 或真实游戏通过。
5. 更新 #191/#179/#180 和协调记录，完成后停止本任务的等待自动化。main 合入依最新维护者接受状态处理，不把发布授权解释为跳过仍未满足的 main 验收门槛。

## 本次没有宣称完成的 #191 目标

按引擎分流的 RE/通用/专桥策略、任意第三方手工 DLL 的无歧义迁移、RenoDX HDR 独立角色、Own-Core/Vulkan 正式路线、全部真实游戏装/卸验收尚未完成。本次不关闭这些目标，也不把既有 CPU/IO 用户现场的唯一根因判定为已经证明。
