# DLSS 5 AI 超分管理器 · 装机宅版

一个面向中国普通玩家的 **NR→SR 安装、设置与修复基础工程**。

它不是新的游戏内 HUD，也不尝试重造 ReShade。桌面端只负责把现有
NR-before-SR 插件变得更容易安装和调节：打开软件即可看到游戏，卡片内
直接一键安装、修改常用参数、检查组件、修复或卸载。

## 当前范围

- Windows 10 / 11；
- 64 位 DirectX 12；另提供单独的 DX11 实验兼容槽，仅对 DX11 游戏自动推荐；
- 游戏本身已经集成普通 DLSS Super Resolution；
- 使用现有 ReShade Add-on 作为插件宿主和游戏内设置页；
- 检测到反作弊游戏时会先弹出风险警告；用户明确确认后才尝试安装，不修改、不删除也不绕过反作弊文件。模拟器、x86、Vulkan、OpenGL、Feeder、OptiScaler 仍不支持。

这些限制是有意的：先把最常用、最安全的一条链路做稳定，再逐层扩展。

## 使用源码

```powershell
git submodule update --init --recursive
npm install
npm start
```

通过 GitHub 的“Download ZIP”获取源码时，压缩包没有 submodule 元数据，请改用：

```powershell
./scripts/bootstrap-vendor.ps1
npm install
npm start
```

管理器可以在没有 payload 时启动、扫描游戏和查看兼容性；安装操作会被
安全阻止。准备发布组件的方法见：

装机宅版 0.4.5 也支持把 RTX 20/30/40 与 RTX 50 两套固定运行组件放进同一个发行包，启动时自动识别
显卡并选择对应组件；游戏页支持选择游戏目录或直接选择实际运行 EXE，替换已有文件时
会保留同目录 `.bak` 回滚副本。未知图形代理和反作弊文件不会被自动删除。
当前安装器版本是 `0.4.5`（`DLSS5-Manager-Setup-0.4.5.exe` / `DLSS5-Manager-0.4.5-portable.exe`）。详细使用方式见 [`docs/INSTALL-GUIDE-0.2.0-BETA.md`](docs/INSTALL-GUIDE-0.2.0-BETA.md)。

```powershell
./scripts/prepare-payload.ps1 `
  -NrBuildDir D:\nr-build `
  -NeuralRuntime D:\runtime\nvngx_dlssnr.dll `
  -ReShadeAddonRuntime D:\reshade\ReShade64.dll
```

脚本会复制必要文件并生成 SHA-256 `bundle.json`；正式分发包将每个显卡族的固定 DLL 只携带一份，RTX 20/30/40 共用 RTX40 兼容组件，版本切换主要替换 addon 和配置。发行包默认使用 `0.3.3.5` 稳定槽，标准 DX12 测试槽只保留 `Beta0.4.5.2`。DX11 游戏在默认状态下会自动选择单独的 Beta0.4.2 兼容测试槽（内部 ID 仍为 `0.4.2-dx11-native-bridge-exp1-r1`）；它可能无法工作或影响启动，遇到问题请进群联系 `@群RBQ` 提交报告。内置版本包括历史
`0.2.0-beta.2`（2K27 兼容候选）、`0.3.3.5`、`Beta0.4.5.2` 标准 OTA 更新槽和 `Beta0.4.2` DX11 兼容测试槽。0.4.x 共用显卡族固定 `nrchain_nvngx.dll`；标准 OTA 会验证包内 Addon、chain、文件大小和 SHA-256，应用时同时替换包内提供的 Addon 与配套 chain，并保留 INI、ReShade、Runtime 及游戏自带 DLSS。

## 用户路径

1. 自动扫描或“添加游戏”；
2. 在主页搜索框/筛选器中定位游戏；
3. 在“设置”里选择要测试的 addon 版本，再在游戏卡片点击“一键安装”；
4. 原地展开卡片，只调整 Addon 版本、风格和人脸保护；NR 工作分辨率、AI 强度、颜色跟随等细项在游戏内 ReShade Overlay 中调整；
5. “插件版本”页优先支持选择/拖入标准 OTA `.zip`，也支持 `.addon64`；双击关联文件也会自动导入。导入 OTA 后，已经锁定旧版本的游戏不会被全局选择覆盖，只有在该游戏卡片点击“应用版本”才会替换；
6. 游戏内仍使用原来的 ReShade 设置页；
7. 切换版本后点“应用版本”即可替换 addon，旧版本会保留 `.bak` 和管理器备份记录；必要时可尝试可回滚的 D3D12 入口修复。

### 版本锁定、OTA 应用与诊断

游戏卡片记录自己的 `payloadVersion`。因此，某个游戏已经锁定 `0.3.3.5` 时，即使插件版本页又导入了新的 OTA，诊断和“修复”仍会以该游戏锁定版本为准；在卡片中明确点击“应用版本”后，才会切换到新版本。应用过程中只执行一次游戏库刷新，并且旧的异步诊断结果不会覆盖新结果，避免出现卡顿、安装已经完成但右侧仍显示旧版本的问题。

标准 OTA 里的 `nr-before-sr.zh-CN.addon64` 与 `nrchain_nvngx.dll` 会作为一个配套更新保存。应用时两者都会先备份为 `.bak` 或进入管理器备份目录后再替换；INI、ReShade、AI Runtime 和游戏自带的 `nvngx_dlss.dll`、`nvngx_dlssg.dll` 不会被 OTA 覆盖。仅导入单独 `.addon64` 时，则只替换 Addon 并保留当前 Bridge。

### 一键安装失败怎么反馈

不要只截“操作失败”的提示。保留管理器打开，进入左侧“修复”页面，在“安装失败反馈”里选择对应游戏，点击“保存反馈日志”，把生成的 TXT 发给维护者。管理器会记录错误码、最近一次操作、组件匹配结果，以及游戏目录旁的 `ReShade.log`、`nr-before-sr.log` 等相关日志；默认会脱敏用户目录，不会自动上传。只有在排查权限或路径问题时，才需要勾选“包含完整路径”。

0.4.x 中 Mode 是旧版内部处理链路，不等于游戏 DLSS 画质档位；当前默认 `WorkMode=0`（Reference）并将 `CustomWorkScale=1.0`，让 NR 工作栅格与游戏当前渲染栅格匹配。`WorkMode` 仍由插件配置兼容保留，但管理器不再把它暴露成独立选项，避免和游戏内渲染分辨率混淆。

## 安全模型

- 所有安装组件必须匹配 `bundle.json`；
- 写入前确认游戏已关闭；
- 检测到反作弊时先警告，用户确认后才继续写入；
- 不覆盖未知 `dxgi.dll`；
- 每次操作使用写前日志，失败自动回滚；
- 首次被替换的文件单独备份，卸载时恢复；
- 默认保留用户的 `nr_before_sr.ini` 参数；
- 安装/修复会把 `renodx-dlss5.addon64`、旧 `nr-before-sr*.addon64` 和可识别的
  DLSS/NR/NGX 冲突 Addon 移到 `_DLSS5_Backup/conflicts`，不删除；
- 不移动 ReShade 本体 `dxgi.dll`、游戏自带 `nvngx_dlss.dll` / `nvngx_dlssg.dll`，
  也不移动普通 RenoDX 色彩/HDR 插件；
- 不在仓库提交 NVIDIA runtime 或其他未审查二进制。

## 上游与署名

桌面基础通过固定版本 Git submodule 复用了 MIT 许可的
[DLSS5-Swapper](https://github.com/rakanki911/DLSS5-Swapper) 部分本地扫描与
文件安全实现。原作者和许可证见 `UPSTREAM.md`、`LICENSE` 与
`THIRD_PARTY_NOTICES.md`。

产品维护：**野生的装机宅**。品牌名、Bilibili 主页、群号和更新地址统一在根目录 `product.json` 配置，空群号不会显示入口。
