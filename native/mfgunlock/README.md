# MFG Unlock 0.7 中文面板构建

默认版本 `mfgunlock-0.7-zh-CN` 基于 mavismmg 的 MIT 项目
[MFGAdaUnlock-RenoDx 0.7](https://github.com/mavismmg/MFGAdaUnlock-RenoDx/releases/tag/0.7)，
固定提交 `ffe6169b5e98ad578fcf2c30614d06a567790fe1`。
原始版权、许可与 README 均保留在 `upstream` 中。

`addon.cpp` 只调整四个相对 include 路径，并把 `OnRegisterOverlay` 替换为
`panel_zh.inl`。运行时 hook、默认值、INI 键、导出/注册名、英文诊断日志
均与固定上游一致。`verify-fg-mfgunlock-source.js` 会逐文件检查原始源摘要，
并核对面板以外的主源文件完全相同。

界面常用项是跟随游戏、2×、3×、4×；已有 5×/6× 配置会保留并标为实验请求，
不会列为默认可选项。详细行为在“使用说明”，低频参数在“兼容设置”，诊断在
“运行详情”。此版本不增加 Dynamic 控制。

上游 0.7 默认保持：`HDRCompatibilityMode=2`（自动输入保护）、
`DepthEdgeGuardLevel=0`、`TemporalFix=1`、`ForceFlipMeteringOff=0`。
倍率覆盖只提高较低游戏请求，FG off 仍透传。`Enabled` 是能力解锁条件，
不是能实时撤销已加载补丁的补帧总开关。

字体由 ReShade 宿主管理。面板使用 UTF-8，并检查当前字体能否显示中文；
缺字时显示可读英文和宿主字体设置提示，不修改或重建宿主字体图集。
真实中文验收使用 ReShade 6.8.0.2155、语言 `zh-CN`、
`C:\Windows\Fonts\msyh.ttc`。依赖锁使用 ReShade API 18 / ImGui 1.92.5。

从项目根目录构建：

```powershell
node scripts/verify-fg-mfgunlock-source.js
./scripts/build-fg-mfgunlock.ps1 -OutputDirectory ./build/mfg-zh-new
./scripts/prepare-fg-mfgunlock.ps1 -CandidateDirectory ./build/mfg-zh-new
node scripts/verify-fg-mfgunlock.js
```

构建需要锁定版本的 SDK 头文件、Detours 源码归档和 Visual Studio 2022
14.44 / Windows SDK 10.0.26100.0。使用 `-DependencyRoot` 和
`-DetoursArchive` 指定本地只读来源；脚本先检查锁中 SHA-256，再复制到新输出
目录编译，不修改原依赖目录。`source-lock.json` 记录精确仓库提交、归档与头文件
摘要。`/Brepro` 的两次独立目录构建已产生完全相同的 Add-on：

`2cd3a44e78429da4b07b2a50d694750e7b3dd34bf4ce007207f1635cf4a70d4e`

`build-mfgunlock.json` 记录实际编译输入、输出摘要和 ABI。编译成功本身不会标记
GPU 或面板验收通过；独立验收记录区分中文/ABI/INI 与实际游戏 MFG 输出。
本构建不分发 NVIDIA DLSS-G 或 Streamline 运行库。
