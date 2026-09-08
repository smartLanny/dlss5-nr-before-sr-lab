# 现用 xiaofeng 管理器维护与待发布交接

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
