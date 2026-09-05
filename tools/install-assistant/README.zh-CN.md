# DLSS5 安装助手

当前建议先使用 **beta0.4.2 增强推荐版**；遇到兼容问题再使用 **beta0.3.8 兼容版**。安装助手会自动核对本机 NVIDIA RTX 代际与完整组件包，避免把 RTX 50 和 RTX 30/40 运行库装反。

中文安装包首次安装时，如果 ReShade 没有设置界面字体，助手会使用系统中已有的微软雅黑，找不到时再尝试宋体。已有的非空 `Font` 设置不会被覆盖；对 `ReShade.ini` 的补充会进入同一安装事务，失败、恢复或卸载时可还原原文件。

首版支持 Windows x64 上的《赛博朋克2077》和《巫师3》D3D12 版本。支持扫描 Steam 和手动选择真实游戏 EXE。其他游戏只供查看，Helldivers 2 尚未纳入自动安装。发现游戏或安装文件成功，不等于已在游戏内生效。

## 使用

1. 解压安装助手。RTX50 与 RTX30/40 的 NR 运行库不同，请下载对应显卡、语言的“完整组件包”。组件 ZIP 不需要解压。
2. 完全退出游戏，以普通权限运行 `DLSS5InstallAssistant.exe`。
3. 扫描 Steam 或选择真实游戏程序。《巫师3》请选择 `bin/x64_dx12/witcher3.exe`。
4. 点击“导入完整组件包”，直接选择下载的组件 ZIP。也可以先解压，再选择目录中的 `manifest.json`；签名文件和全部载荷必须在同一目录。
5. 点击“安装 / 更新”。助手会准备固定版本的官方 ReShade 6.8 FullAddon，检查现有文件，再统一备份和部署。可先点“查看安装计划”查看逐项目标。
6. 点击“启动游戏”，载入场景。用“刷新运行状态”检查实际加载路径、插件哈希与本次安装是否一致，以及是否有连续 NR 提交。

通过 Steam 启动时，助手会在游戏进程真正出现前持续锁住安装目录，避免启动器延迟期间被另一实例更新。等待 45 秒仍未找到所选 EXE 时保护会结束；如果游戏随后才出现，请不要安装、恢复或卸载，先退出游戏再操作。

ReShade 只从固定官方地址下载，安装程序和实际宿主均验证固定 SHA256。官方安装程序只在临时缓存沙盒内无界面运行；游戏目录由助手事务统一管理。网络不通时可在“高级选项”导入官方 `ReShade_Setup_6.8.0_Addon.exe`，同样必须通过固定哈希验证。

已有相同官方宿主会复用。已有 ReShade.ini 且没有 `[ADDON] AddonPath` 时使用 ReShade 默认的 EXE 目录；首次干净安装使用游戏目录内的 `.dlss5-addons`。addon、bridge、NR 运行库、插件 INI 和包内离线反馈脚本放在同一个 addon 目录。首版拒绝游戏外共享 AddOnPath。

用户改过的插件 INI 原样保留，升级不会覆盖强度、颜色跟随或其他设置。若现有 INI 与签名清单声明的已知默认值完全一致，切换版本时会更新为新版本默认值；安装计划会明确说明是更新默认配置还是保留用户配置。新安装采用所选签名组件包中的默认值。已验证旧插件会先备份后移除，避免多个版本同时加载；未知同名 DLL、未知 NR 插件或另一宿主入口会保留并停止安装。

## 取消、卸载与恢复

安装过程中可以取消。文件写入、安装记录与备份按事务关联；中断后可用“高级选项 → 恢复中断事务”。旧的已回滚事务不会影响后来成功的安装。

“卸载并还原”仅处理助手实际拥有的改动，复用文件不会删除。用户后来修改或删除的文件会保留。原始备份缺失或哈希不符时，助手保留当前文件及恢复记录，并明确报告未全部完成。

游戏运行时拒绝更改安装。所有助手实例和提权 helper 对同一游戏串行操作；操作中不能同时从助手启动游戏。需要写入受保护目录时才请求管理员授权，界面会等待 helper 的真实结果。请勿以管理员身份启动整个助手；游戏启动仅允许普通权限。

## 如何确认生效

“已部署”只表示安装文件成功。“已加载”还需要当前游戏进程发出 v2 状态，并且实际 addon 路径、SHA256、构建名、源码提交与安装记录一致，源码必须是干净构建。只有当前配置下的连续 NR 提交才显示“NR 正在提交”。相同版本号、旧进程、过期状态、旁路、仅有创建成功都不能代替这个结果。

本版本完成了自动事务、签名、路径、恢复与官方宿主缓存验证。真实游戏中的安装升级、视觉效果和兼容性仍需按当前候选实测；不据此声称所有游戏、HDR 或所有显卡验证通过。F8 可在游戏内采集反馈；无法进入游戏时，可使用 addon 同目录的 `Collect-Feedback.cmd`。

作者：@野生的装机宅 Bilibili

## 开发与打包

`scripts/test-install-assistant.ps1 -VerifiedHostPath <官方6.8完整addon宿主>` 运行生产接口回归。真实官方缓存准备由 `tools/install-assistant.HostTests` 验证。测试只使用临时合成游戏目录，不启动真实游戏。

`scripts/build-install-assistant.ps1` 发布 WPF 与 helper 的 Windows x64 自包含程序。最终提权功能要求应用目录带有本项目固定公钥对应的签名清单。

`scripts/package-install-assistant-release.ps1` 必须显式提供 `-PayloadCatalogPath`、`-LegacyCatalogPath`、`-OutputRoot`，源码提交后才可运行。它不会按旧版目录或 glob 猜载荷，逐文件核对输入 SHA256，并在压缩前签名、复验应用目录与每个组件目录。每个 ZIP 都带钉死项目公钥身份的 `Verify-ReleaseManifest.ps1` 手工验签脚本；应用和组件 ZIP 文件名均包含插件版本，输出根目录的 `DELIVERY_EVIDENCE-<插件版本>.json` 记录实际生成的应用 ZIP 与四个组件 ZIP 的大小、SHA256、安装助手提交和插件载荷提交。

载荷目录清单 schema：`dlss5-installer-payloads/v2`，包含 `build`、`edition`、`payloadSourceCommit` 和四个 `variants`。`edition` 只接受推荐的 `enhanced-recommended` 或兼容兜底的 `compatibility-fallback`；HD2 等实验包不能自动安装。每项 variant 声明 `gpuFamily`（RTX50/RTX30-40）、`language`（zh-CN/en）、`files`（每文件的绝对 `sourcePath`、`sha256`、`role` 与独立 `source` 来源）。复用旧包中的大运行库时会记录旧包来源和运行库哈希，不会冒充本轮新构建。必需角色为 addon/bridge/runtime/config；可选角色为 feedback-launcher/feedback-collector 和 crash-feedback-launcher/crash-feedback-collector。两个崩溃采集角色只接受固定文件名 `Collect-Crash-Feedback.cmd`、`collect-crash-feedback.ps1`。

旧版清单 schema：`dlss5-installer-legacy/v1`。`aliases` 列出允许迁移的旧 addon 文件名及 SHA256；`knownFiles` 列出允许更新的固定文件名及 SHA256。未知文件不通过名字推断归属。产出的安装 manifest 使用 v4，将用途、源码提交和插件 artifactId 绑定到实际 SHA256。
