# 本地 Agent 执行交接：把已有 Feeder 接到当前 D14，不重造框架

## 权威、当前状态与本轮新增

Core/输入：`smartLanny/dlss5-nr-before-sr-lab`，继续 PR #250 / 分支
`codex/050-fullres-optical-inputs-20260912`，父提交 `737e65cdc1641ad5e3945e8fcb40acf1fa431f23`。
目标是维护者选定的 `codex/050-corefix8-integration-20260911`，本轮读到
`49b0deb0d09b0a442784e0ebf1073f9498eaf157`。不把 main/旧0.4.7覆盖到 D14。

Manager 权威：公开 `smartLanny/DLSS5-Manager-ZJZ`，继续 PR #3 / 分支
`codex/bridge-dlc-contract-20260912`，新增提交 `adecf8e5d56e7eddffbdaaa541d8bae16d721801`。
它已把显式本机 JSON 接进真实 CLI 的桥接预检，但仍不下载/安装 DLL；不是旧私有UI的整体迁入。

本轮修正光流 probe 的跨代次旧结果发布、设备移除/超时的无穷busy显示、
手动启用FX绕过默认关闭、错误效果顺序导致旧Capture冒充原图；增加 HRESULT。
新增 `probe_state.hpp` 在真实 backend/callback 使用；不是只交测试账本。
增加真正调用 Windows D3DCompile 的三shader变体编译程序，以及 `validate-local.ps1`。

**未完成：Windows addon/HLSL/FX 编译、真实 OF、Feeder→D14 Feature18、游戏画面和性能。
#250 仍是 guide-only，不是可安装的NR兼容版。不能直接合入就宣称任务完成。**
云端本轮仅实际执行了portable C++与Node局部测试；Windows验证工具尚未运行。

## 先安全接续，不回退本地

在 ROG 已有授权 Windows 项目工作区检查 remote、status、worktree、最新目标分支和
两个PR。保留未提交工作；不能 reset --hard、clean、自动stash或强推。使用独立 worktree。
本地有比上述D14更新的Core时，以实际最新Core为基线，只移入尚未包含的PR增量，
保留双NR、画面控制、描述符/队列修复。记录真实基线→整合提交，不能只复述云端SHA。

私仓原始反馈、签名/密钥、模型、运行DLL和本地路径不得进入公仓。不要复制旧Manager树。
不删除Feeder/Bridge旧版缓存；运行时每条路线只激活一个明确配套的输入提供者。

## 执行顺序：以真实非DLSS游戏进入当前Core为目标

### A. Windows编译与可用光流证明

先读 `experiments/fullres_guides/README.md` 和本文件。
`validate-local.ps1 -EvidenceDirectory <仓库外全新目录>` 默认只编译portable测试与
实际 Windows D3DCompile checker，不跑GPU、不装游戏。用单独新目录加 `-BuildAddon`，
显式提供 `-FfxRoot`、`-FfxLibraries`、`-NvofInclude`、`-ReshadeInclude`、`-ImguiInclude`
编译整个真实 addon。官方FidelityFX固定v1.1.4/OF1.1.2，检查库与头一致，不补假SDK声明。
缺路径去已有授权依赖目录核对，不为让configure成功创建空.lib或关闭安全检查。

脚本保存命令退出码、源hash、产物hash，失败证据不覆盖。其成功不包括ReShade FX编译，
后者必须在实际ReShade测试宿主加载 `NR_FullResGuides.fx` 后另行记录。

在隔离D3D12 GPU宿主做固定源/参考对：静止、水平±16px、垂直±16px、对角移动、
显式reset与奇数尺寸。AMDOF/NVOF各自100%/50%；检查current→reference方向、幅度、
首次/reset全零的明确标记和源图未被修改。CPU有限值或“提交成功”不能代替光流正确。
同时检查50%只改变OF副本、actual extent比例和输出网格，不能改变NR Color/Depth。

### B. 复用已有Feeder消费接口，前向接入D14

现有配套有 `NRExternalProviderV1` 专用消费者。先找其真实源码、生成器和构建清单。
历史Manager manifest引用 `4ecc6d02ca6058cb1ebb0faae5aa49b34b0614ad`，云端此前读此ref为404；
核对本地 git 对象/source-manifest，不能根据manifest假装源码已找到。#175可作为已知
项目自有接口参考，但只移植当前需要的部分，不整分支覆盖D14，不再增加第二套Feeder。

核对 ABI size/version、Query真实ready、Claim单一NR owner、Submit实际录制/执行/fence、
Release/退出。没有provider时不抢占原生NR，不强制加载Feeder，不改变普通Core依赖。
不同Core/ABI拒绝该可选路径，明确保留原图并提示F8/日志；禁止自动换回旧Core掩盖不兼容。

把匹配的Color、MV、Depth、颜色编码和reset送入D14已有NR/双层/合成代码，
保留现有格式/资源/命令列表/队列生命周期保护。不要只为唤醒自家NR再注入一次DLAA，
不要开启第二个NR/SR/FG所有者。RR与SR不得被误认为同一特征。

### C. 将可复用的OF模块接到同一个Feeder输入路径

优先复用现有capture/Generic Depth/host/transport。OF在私有的原始Color副本上运行，
不能捕获已NR增强图或FG生成帧再拿来做自己的历史。默认原生输入优先，合成实验默认OFF；
只有 AMDOF/NVOF 和 100%/50% 两个选择，AMD/100默认，不自动降档、不加其它比例。

重要：当前Session历史追踪的是“OF已提交帧”；接NR后必须追踪“消费者实际接受帧”。
若OF成功但NR未接受，不准不加处理地把它当下一帧NR reference；重配或reset且记录原因。
旧结果保留预览用途。`result(view, ...)` 是明确可迟到的预览接口，不是同帧NR消费接口；
禁止拿旧preview配新Color。需要保存并提交同一份不可变源快照、Depth和MV及完成凭据。
NV cost/FFX场景切换能提供多少按真实SDK合同接，不能固定置信度冒充测量，也不是人脸mask。

### D. 最小真实验收，不无限设计新框架

先一款已有授权、允许mod的离线非DLSS D3D12游戏，证明输入进入当前D14并真实回填；
再复用现有DX11 transport。DX9/x86/Vulkan沿已有实现扩，不一次重写所有API。
先不加FG和独立SR，避免混在一起定位；后续才测它们共存。

必须覆盖：无Feeder原生游戏不回退；无原生且缺DLC明确旁路；原生guide优先；
AMD/NV×100/50；开关/切比例/reset/resize/Alt-Tab/效果重载；后端失败/队列超时；
原始/增强的同源帧截图及F8/ReShade日志；完整OF+转换+等待+NR的耗时。
F6旁路应保留原游戏；任何旁路明确要求收集反馈，不能“loaded”就显示兼容完成。

## Manager任务独立，不阻塞GPU主任务

在公仓检查PR3并跑 `npm ci && npm run check`；真实CLI显式上下文入口见
`docs/BRIDGE-CLI.md`。无上下文保留旧dry-run；被拒绝的可选计划退出码1，绝不安装。
后续从真实已批准的DLC库存收集相同context，按版号+变体+hash+API/位数+Core接口检查。
本轮CLI输入hash不是认证，仍需签名/实际文件校验、游戏退出、备份与事务恢复。
不要为了过测试给通用配方任意指定Feeder pin；批准配套后再登记。多个版本可以缓存，
同一游戏不能一起加载。SDK/模型/Shader许可逐项核对，DLC不是再分发许可豁免。

## 交付与停止条件

直接完成可完成的实现与验证，小范围修复后提交当前PR，不重复开总Issue。
在私仓#239/PR250报告GPU/Core结果；公仓#2/PR3只报告安装/CLI结果。
给维护者：实际代码SHA、编译/CI/GPU/游戏分别状态、产物SHA、四组画质/耗时与恢复方法。
若卡在不可取得的源码、SDK或权限，给出具体命令/错误/缺文件；不要重新架构或继续堆空接口。
未经维护者确认不merge main、不发Release、不改稳定包；不自动更新现用游戏安装。
