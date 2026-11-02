# DLSS5 NR-Before-SR Lab

**装机宅 DLSS5 增强 Core：NR → SR、SR → NR，以及最多五层独立模型。**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Source: Beta 17](https://img.shields.io/badge/Source-0.5.2%20Beta%2017-5b6cff.svg)](scripts/core-source-layout.json)

[开始使用](#开始使用) · [当前能力](#当前能力) · [开发与测试](#开发与测试) · [报告问题](#报告问题) · [English](#english)

本项目在游戏的升频链路中加入 Neural Rendering，并提供模型参数、最终画质调节、
安全旁路和诊断。`main` 是唯一持续开发分支；安装管理器独立维护在
[DLSS5 Manager](https://github.com/smartLanny/DLSS5-Manager-ZJZ)。

**当前源码为 `0.5.2-beta.17` 开发候选。源码合并不等于新版安装包已经发布，也不代表
所有游戏完成验收。** 已验证的 Beta14 交付、组件哈希和回退身份保存在
[交付回执](docs/interop/052_BETA14_DELIVERY.json)。

## 开始使用

| 你的目标 | 从这里开始 |
|---|---|
| 安装和管理插件 | [Manager 项目与安装说明](https://github.com/smartLanny/DLSS5-Manager-ZJZ) |
| 了解功能、限制和验证状态 | [当前状态](docs/interop/CURRENT_STATUS.md) |
| 编译、贡献代码 | [开发入口](START_HERE.md) · [贡献指南](CONTRIBUTING.md) |
| 核对模型 DLL 身份 | [网络选择与运行库验证](docs/validation/NR_NETWORK_SELECTION_20261018.md) |
| 遇到失效、闪烁、崩溃或开销异常 | [提交 Issue](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/new/choose) |

建议先保持一层和普通恢复，确认增强正常，再逐项比较工作比例、模型网络和多层效果。
游戏菜单、过场、HDR、帧生成及运行库组合都会影响实际结果；一次只改变一个变量，
并保留可回退的配置与安装包。

## 当前能力

| 能力 | 使用方式与边界 |
|---|---|
| NR → SR / SR → NR | 根据游戏输入与资源合同运行；不安全时保留原链路并报告原因 |
| 原生 RR → 后置 NR | 已有产品路线；双 RR / Denoise-first 仍是独立研究 |
| 一至五层模型 | 共用工作尺寸，各层可独立调节模型参数和网络；停用层保留设置 |
| 模型网络选择 | 全局默认、Original/HT、Optimized/Small FP8；各层可继承或覆盖。请求值不等于运行库已执行 |
| AI 色彩与最终增强 | 常用设置集中在“画面”，增亮、压暗和光影细调放在“光影” |
| Continuous SDR 微调 | 默认关闭的可选算法；只用于明确的 SDR 输入合同，HDR/未知输入不自动套用 |
| F8 反馈与 F7 图像补充 | 记录版本、请求、实际路径与失败原因；局部 GPU 时间不能替代整帧性能 |

### 菜单怎么找

顶部先显示**版本与运行状态**，再显示当前保存的设置摘要。

- **画面：** 总开关、AI 色彩、最终增强、增强位置、工作比例和层数。
- **模型：** 全局网络、逐层网络及模型参数；当前启用层可一键恢复为跟随全局。
- **光影：** 调节最终增亮、压暗与细节变化。
- **实验 / 开发者 / 反馈：** 可选研究功能、底层设置及诊断信息。

私有 SR、空间与时域研究的画质收益仍需逐项验收。更多层数不保证更清晰，也会增加
模型工作与显存需求。尚未承诺通用 FG 注入、全 API 支持或固定百分比的游戏性能提升。

## 开发与测试

```sh
git clone https://github.com/smartLanny/dlss5-nr-before-sr-lab.git
cd dlss5-nr-before-sr-lab
python3 scripts/run-portable-tests.py --verbose
```

使用主机 C++ 编译器，可用 `CXX=g++` 或 `CXX=clang++` 指定。首次克隆保留完整历史，
部分回归需要历史源文件。跳过的测试不是通过。

Windows 完整构建使用 `scripts/build-unified-candidate.ps1`，需分别准备固定的 carrier、
ReShade、DirectXMath、LLVM-MinGW、人脸后端和 FFX SDK/缓存。详见
[构建结构](docs/maintenance/SOURCE_LAYOUT.md)和[新机说明](docs/handoff/BETA13_NEW_PC.zh-CN.md)。
源码审查、CPU、编译、WARP、真实 NGX 与游戏验收分别记录，不能相互替代。

## 报告问题

请说明游戏、GPU、驱动、插件版本、运行库身份、增强位置、工作比例、层数、HDR/FG
状态和复现步骤。持续旁路请按界面提示收集 F8；画面问题可补充 F7 对照。
**提交公开 Issue 前检查并脱敏文件中的用户名、路径及其他个人信息。**
遇到崩溃或严重画面问题，先恢复已验证的安装包和配置。

## 开源与依赖

原创源码采用 [MIT](LICENSE)。RenoDX、MinHook 和 Oklab 等来源及授权保留在
[第三方声明](THIRD_PARTY_NOTICES.txt)；欢迎提交有来源、有验证依据的改进。
借鉴其他项目时须保留对应许可和署名。

公开 Core 包不附带 NVIDIA NR 模型/运行库。源码授权不改变 NVIDIA、ReShade、游戏及
其他独立组件的条款；自行准备的运行库请记录版本与 SHA256。SHA256 是完整性校验，
不是数字签名。本项目不代表 NVIDIA 或其他相关厂商。

## English

An MIT-licensed Neural Rendering enhancement Core for NR-before-SR, post-SR NR,
native RR followed by NR, and up to five independently configured model layers.
The Beta17 source candidate includes per-layer network requests and optional
Continuous SDR controls. Source availability does not imply a released binary,
verified model execution, or acceptance in every game.

Use [START_HERE.md](START_HERE.md) and [CONTRIBUTING.md](CONTRIBUTING.md) to build
and contribute. Preserve native rendering, resource lifetime contracts, saved
settings and third-party notices. NVIDIA runtimes are obtained separately.
