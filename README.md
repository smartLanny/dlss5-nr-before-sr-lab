# DLSS 5 AI 超分管理器 · 装机宅版

一个面向中国普通玩家的 **NR→SR 安装、设置与修复基础工程**。

它不是新的游戏内 HUD，也不尝试重造 ReShade。桌面端只负责把现有
NR-before-SR 插件变得更容易安装和调节：打开软件即可看到游戏，卡片内
直接一键安装、修改常用参数、检查组件、修复或卸载。

## 当前范围

- Windows 10 / 11；
- 64 位 DirectX 11 / DirectX 12；
- 游戏本身已经集成普通 DLSS Super Resolution；
- 使用现有 ReShade Add-on 作为插件宿主和游戏内设置页；
- 不支持反作弊游戏、模拟器、x86、Vulkan、OpenGL、Feeder、OptiScaler。

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

```powershell
./scripts/prepare-payload.ps1 `
  -NrBuildDir D:\nr-build `
  -NeuralRuntime D:\runtime\nvngx_dlssnr.dll `
  -ReShadeAddonRuntime D:\reshade\ReShade64.dll
```

脚本会复制五个必要文件并生成 SHA-256 `bundle.json`。

## 用户路径

1. 自动扫描或“添加游戏”；
2. 在游戏卡片点击“一键安装”；
3. 原地展开卡片，调整性能/画质模式、AI 强度、风格和人脸保护；
4. 游戏内仍使用原来的 ReShade 设置页；
5. 出问题时进入“修复”，检查组件并一键恢复。

## 安全模型

- 所有安装组件必须匹配 `bundle.json`；
- 写入前确认游戏已关闭；
- 检测到反作弊时停止；
- 不覆盖未知 `dxgi.dll`；
- 每次操作使用写前日志，失败自动回滚；
- 首次被替换的文件单独备份，卸载时恢复；
- 默认保留用户的 `nr_before_sr.ini` 参数；
- 不在仓库提交 NVIDIA runtime 或其他未审查二进制。

## 上游与署名

桌面基础通过固定版本 Git submodule 复用了 MIT 许可的
[DLSS5-Swapper](https://github.com/rakanki911/DLSS5-Swapper) 部分本地扫描与
文件安全实现。原作者和许可证见 `UPSTREAM.md`、`LICENSE` 与
`THIRD_PARTY_NOTICES.md`。

产品维护：**野生的装机宅**。品牌名、Bilibili 主页、群号和更新地址统一在根目录 `product.json` 配置，空群号不会显示入口。
