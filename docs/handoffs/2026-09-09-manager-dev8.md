# Manager dev.8 本地验收候选

Manager **0.4.6-hotfix.7-dev.8** 接续 #195，修改现用 xiaofeng 界面。冻结的 manager-launch-controls 测试 UI 没有继续开发。此候选不作为公开稳定版发布。

## 使用体验

- SR/FG 选择停顿 500ms 后自动应用，连续编辑合并；移除保存到下次启动、预览、再次确认的普通流程。写入仍检查当前 EXE、游戏进程、组件和外部改动，并保留 journal/驱动恢复记录。失败保留选择与重试入口。
- 一个“恢复默认”撤销两域管理器覆盖；其中一域失败会展示未完成，不冒充全部成功。恢复原有配置不等于关闭游戏 FG。
- 自动应用保存并恢复滚动位置，不因重建表单跳到下面。SR/FG 页签保持按需读取。
- API 与核心仍在顶部；D3D12 入口直接显示为“兼容修复”，说明用于异环等部分 DX12 网游的加载报错、DXGI 入口冲突。非 DX12 不允许启用，撤销仍保留。
- 自动扫描和手动添加的游戏均有改名入口。使用现有风格的应用内弹窗，支持预填、Enter、取消和空名验证，仅改显示名称。
- 一键安装显示转圈、正在安装和可访问的忙状态；成功、取消、失败后清理忙状态，不伪造百分比。
- ReShade 快捷键使用 Windows OEM 虚拟键映射，补齐 `= - ; ' [ ] \\ , . /` 和反引号。英文/中文、Shift 键盘事件共 44 组真实 INI 写入读回覆盖，保留 BOM、CRLF 和其他键。

RTX40 会检查并准备 RTX40 MFG v1.2 / UAL v9.7.4 配套组件，RTX50 使用已有原生 FG 和驱动设置路线。需要游戏已有受支持的 FG；不等于任意无 FG 游戏注入。可以请求 2–6×，40 系 5/6× 明确为实验项；实际采用取决于游戏、驱动与组件，游戏菜单开关不能直接读回。既有“仅显示生成帧”调试模式不会被普通自动应用默默带入，需先从组件关闭该模式。

## 升级、冲突及历史版本

保留 **0.2.0 历史兼容、0.3.3.5 稳定基线、0.4.2 Beta、0.4.6-hotfix.1 Beta**。0.2 内部历史 ID 仍为 `0.2.0-beta.2` 以兼容旧记录，显示名不标 Beta。新补的普通 0.4.2 经过原中文完整 ZIP、原源码标签及文件摘要核验；没有把 DX11 实验版改名当普通版。

0.4.2 addon SHA256 `ccd7f1d8cd95a8559e5a629d3206bf586777173aa2309722202ec1a0a0b2d1e2`，来源 `beta0.4.2@15909ef10914fcdf151ee193460451667fbf31dc`。普通 0.4.2 不含 carrier。原有三个版本和所有固定组件摘要不变；DX12 不部署 DX11 carrier，共用 nrchain 保留。

修复/卸载新增真实文件 journal 验证：不重复复制未变化文件；本工具新建的 bak 有独立摘要收据，卸载后归档到 `_DLSS5_Backup`；预先存在或外部修改的 bak 不擅自接管。重复同名冲突恢复最早的安装前文件，后续副本保留历史索引。晚期恢复失败会回滚整个卸载，失败不显示成功。

冲突识别不再将 sunrise 等普通名称或仅引用 DLSS/NGX 的第三方 Addon 当作 NR 冲突。按实际 ReShade 6.8 加载配置核对 EXE 目录，默认只检查会被非递归加载的目录；自定义 AddonPath/BasePath、旧直接加载入口给出可读阻断，避免假装清理/部署成功。当前仍没有通用的无收据 Opti/任意手工 DLL 自动迁移，也不等于完整运行时外置加载。

## 启动故障

业务模块初始化之前记录启动阶段；覆盖提权取消/失败、portable 原壳重启、界面加载、preload、renderer 崩溃、GPU 子进程退出、未出现窗口和单实例无回应。窗口前错误有原生可见提示；不能显示业务界面时仍可导出精简诊断。旧配置异常保留原字节恢复副本，未知格式不静默覆盖。

启动日志每份最多 64 KiB，最多四份；报告最多 128 KiB，路径脱敏。独立 `启动诊断.cmd` 不依赖 Electron 或 portable 解包成功，核对实际随包文件、TEMP、有限崩溃摘要和启动日志。已用 Windows PowerShell 5.1 验证中文 UTF-8 BOM 脚本，不能仅以 PowerShell 7 测试代替。

Steam/GOG 的只读路径发现改用一次有界 .NET 注册表读取，避免启动链路依赖 `reg.exe`；受策略限制时返回中文原因，并保留手动游戏和本地默认路径。没有改系统安全策略，也没有默认添加 `--no-sandbox`。既有 “GPU process isn't usable” 反馈尚缺目标机退出码/模块证据，不能宣称其具体根因已修复，也不能一律归因于 VC++。

## 验证边界

- 完整本地回归 **409 passed / 0 failed / 0 skipped**；UI 合同 74 HTML IDs / 70 renderer references。最后仅修改旧调试模式错误文案，相关服务 16/16 重新通过。
- 实际包内 ASAR renderer：SR/FG 等 114 项隔离交互检查，改名/安装忙状态 14 项检查通过。主代理检查 1280×900 和 900×620 布局，保留深色绿色主题和 DLSS5 装机宅图标。界面使用夹具，不是游戏证据。
- RTX5090 Laptop 的独立临时 EXE 配置实测：SR custom 67% / K、FG fixed 4× 的八项 NVIDIA 设置写入/读回匹配，随后 FG→SR 恢复到原八项及 profile 存在性；没有运行临时 EXE、游戏或 GPU 工作负载。详见同目录 `2026-09-09-manager-native-settings-probe.md`。
- Setup/portable 成品、实际载荷/FG 摘要、许可证、源码与 ASAR 字节匹配通过；外部组件版 unpacked 不含 NR payload，保留 FG/NVAPI/注册表辅助脚本及独立启动诊断。
- 实际 portable 外壳和内部 EXE 的 RT_MANIFEST 都为 `requireAdministrator`。实际包内 EXE 通过 `--diagnostics-output` 导出报告并退出 0，未添加禁用 sandbox 参数。
- 通过完整 Setup 更新到现用 `AppData/Local/Programs/xiaofeng-dlss5-manager`，安装返回 0；已安装 EXE/ASAR 与两份资源脚本、两个独立诊断入口均与构建摘要匹配。`build/installed-before-dev8` 保留旧 ASAR/摘要，dev.7 完整 Setup 仍可回退。
- 实际安装版启动日志确认 dev.8、管理员、window-visible 和 renderer-ready；再次启动获得原实例唤起确认并退出 0。本机成功不外推到“GPU process isn't usable”反馈者的电脑。
- Vulkan #165 仅完成部署事务/引用计数/恢复模块与注册表适配，未开放 UI 安装路线。还缺正式 layer recipe、逐 EXE 激活接口、#164 transport 与实际 Vulkan 验收。不能把此模块单测称为 Vulkan 支持。

## 成品摘要

| 文件（dist） | SHA256 |
| --- | --- |
| win-unpacked/resources/app.asar | `6d88a4cdbfb5b47b21e96cafe8ab48f7dec0b1cbf016637771ca144b5ff71d4e` |
| win-unpacked/DLSS 5 AI 超分管理器.exe | `e4b48a6cf0a38314ba109115dabc6d192f507bf6aefdd2ee0ce84b44e52a0842` |
| DLSS5-Manager-Setup-0.4.6-hotfix.7-dev.8.exe | `7f5663e715effcd8d3cce00cdeec2b1b2e6cd24698cb1642dfb5792662200bab` |
| DLSS5-Manager-0.4.6-hotfix.7-dev.8-portable.exe | `b155e474465e94ed2fdcbb4631524250d61391a1a82d88765530ccb9e75bd3f6` |

独立诊断包：`dist/DLSS5-Manager-启动诊断-0.4.6-hotfix.7-dev.8.zip`。本地证据：`build/manager191-dev8-complete-tests.log`、`build/redesign-package-verification.json`、`build/dev8-packaged-ui.log`、`build/dev8-packaged-card-actions.log` 和 `build/dev8-*.png`。

## 后续联合发布

核心任务正式交接为 dev12 / #206：构建源 `420a39ae021d45c06d397390535a01076bff14de`，交接 HEAD `3d16e1ba020e44786c9a19fb4e29c7c0d5163acd`。仅 D3D12 手动 Core 增量，没有新 loader、模型、运行库或 INI，也不是 Manager OTA；不改名为 0.4.6 或混入默认载荷。#204 是独立 carrier 注册表补丁，同样待实际游戏验收。继续按维护者要求等集中验收后联合发布。

#180 已由维护者关闭，当前保留 CPU/IO 修复；#179/#191/#165 与相关实际运行时事项保持开放。Manager 根目录与 Core main 布局不同，最终收敛仍须显式子目录整合，不能原样 merge 删除 Core 源文件。旧工作树的 ignored 回退包和 junction 依赖继续保留。
