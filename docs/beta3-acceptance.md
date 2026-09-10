# Manager 0.4.8-beta.3 验收记录

管理器版本 `0.4.8-beta.3`，Windows 构建版本 `0.4.8.3`。本记录把组件文件核验、受控程序运行、真实游戏结果与最终交付检查分别记录；未完成项保留待测状态。

## 冻结组件与来源

| 项目 | 本批固定身份 | 依据 |
| --- | --- | --- |
| Core | `0.4.7beta`；原生与 `NRExternalProviderV1` 是不同二进制 | `payload/nr-before-sr/bundle.json`；`resources/legacy-runtime/manifest.json` |
| Feeder | `0.15.1`，提交 `3f624855276c4bde55145c712782477639b30e85`；项目 IPC v9 完成回执扩展 | 固定资源清单与源码构建记录 |
| ReShade | 官方完整 Add-on `6.8.0`，x86／x64 分别固定摘要 | `resources/legacy-runtime/manifest.json` |
| VORT | `b410b9f0c0fbb83c8cb42164aaf1655fab386f4a`；相同像素着色器路径 | 每文件摘要、原始许可与受控编译日志 |
| DX9 入口 | 本项目自编 shim，调用 Windows 系统 D3D9On12；x86／x64 | `native/d3d9on12-shim`；固定 shim 与专属 provider 摘要 |
| 原生 DX11 桥接器 | 本项目 NR 适配的 NIGos `1.4.12` 默认／`1.4.11` 回退 | `src/product/component-registry.js` |
| MFG Unlock | `0.7` 中文版默认，官方 `0.7`、`0.6.1` 备选 | `resources/fg-mfgunlock/manifest.json`；`native/mfgunlock/source-lock.json` |
| HoYo 配置参考 | `V3.0.0-Beta.9`，`23761f935444a78388c028fbfda921965177c453` | `resources/hoyoshade/component.json` |

旧 Feeder／Vulkan 固定资源保留各自的收据与恢复范围，不继承新配套的验收状态。dgVoodoo 2.87.4 因更新规则后的 Defender 检测和通用框架分发限制被排除；本批 DX9 使用系统 D3D9On12 替代。原始告警记录见 [下载告警核查](beta3-dgvoodoo-security-review.md)。

## 文件与配方核验

2026-09-10，`npm run verify:legacy-runtime` 通过：35 项受管资产、14 个独立 PE 文件、24 个基础配方、48 个包含显式入口选择的配方。校验器逐个核对固定摘要、PE 位数、目标结构和受控证据，并拒绝资源池内未列入清单的文件或链接；大运行库只各读取一次。

冻结资源指纹：`34820cd0c2f98cf830d85a09e70378bfda30a68a033128d522c0056d0528d80d`。

机器可读的受控证据：[controlled-rtx50.json](../resources/legacy-runtime/acceptance/controlled-rtx50.json)。每条路线记录实际测试目录、进程、文件摘要、NR 完成证据和操作系统退出码。RTX40 与 RTX50 分别使用自己的固定 `nvngx_dlssnr.dll`；RTX40 文件已核验，硬件运行未验证。

## RTX50 受控程序

硬件：NVIDIA GeForce RTX 5090 Laptop GPU，实际测试适配器 LUID `00000000:00014EC3`。范围为 SDR、Synthetic 后处理 NR、VORT 运动估计和一次分辨率重置；不是原生 SR／FG 注入或真实游戏画质验收。

| API | 游戏位数 | 处理位置 | 帧数 | NR 与重置后恢复 | OS 退出码 |
| --- | --- | --- | ---: | --- | ---: |
| DX9 | x86 | 系统 D3D9On12 → x64 宿主 | 600 | 通过，含 NR 独立开关与解绑深度 | 0 |
| DX9 | x64 | 系统 D3D9On12 → x64 宿主 | 600 | 通过，含 NR 独立开关与解绑深度 | 0 |
| DX10.0 | x86 | 共享纹理 → x64 宿主 | 480 | 通过 | 0 |
| DX10.0 | x64 | 共享纹理 → x64 宿主 | 480 | 通过 | 0 |
| DX11 | x86 | 共享纹理 → x64 宿主 | 480 | 通过 | 0 |
| DX11 | x64 | 游戏进程内 | 480 | 通过 | 0 |
| DX12 | x64 | 游戏进程内 | 600 | 通过 | 0 |

DX9 两条路线使用实际 shim、ReShade、专属 add-on、VORT 和宿主链路。DX12 的固定最终验收运行保留了着色器重建时间，所有读回样本 alpha 变化为零。DX10／DX11 的 alpha 变化区域与单独 ReShade 叠加层对照相符，本批没有把它归因为 NR，也没有标注为独立 alpha 验证通过。DX9 的完整 add-on 测试不读取 NR alpha。

DX9 R3 的两架构使用同一源码。受控程序每帧绘制后在 Present 前解绑深度；provider 从本帧实际绘制使用的深度候选中选择来源。关闭 NR 后，提交、回填和完成计数保持不变；重新开启后重建资源与历史，即使 ReShade 全局效果关闭也能继续完成 NR。重置测试先额外绘制一帧并保留深度候选，再执行 Reset，以验证相关引用能释放。x86 最终 NR 完成 408 帧，x64 为 404 帧；两者 600 帧正常退出。来源、两份二进制 SHA、Defender 与受控计数记录在 [dx9-r3-build.json](../resources/legacy-runtime/acceptance/dx9-r3-build.json)，控制和深度约定见 [DX9 provider 说明](feeder-dx9-provider.md)。非 DX9 五路未更换组件，保留原测试记录。

R3 另验证同一 640×360 输出下 D24S8／D16 深度来回切换，实际 D3D12 格式为 45／55，每次正确重建并继续完成 NR。帧 451–480 绑定实际 2 倍 MSAA 深度，提交、回填和 NR 完成计数全部停止；恢复非 MSAA 深度后重新建立历史并恢复处理。`GetDeviceRemovedReason` 为 0。系统未提供 D3D12 debug layer，故不标注 debug layer 验证通过；本机系统 D3D9On12 拒绝创建 D32／D32F，相关失败记录保留，未把它们计作实测通过。

两次更新前各 48 个配方指纹均保留在历史恢复清单，并逐个验证资源池缺失时仍能读取旧收据；篡改收据仍拒绝。旧清单、DX9 二进制和全部配方分别归档于 `build/feeder-d3d9on12-r2-promotion`、`build/feeder-d3d9on12-r3-promotion`。这不授权混装，也不使旧收据自动获得新 provider 的运行验收。

会话证据校验要求当前仍存活的准确游戏／宿主进程、启动时间、EXE 与组件摘要、对应显卡 LUID，以及读取游标之后的新 NR 完成帧。旧日志、已退出进程、单独“助手就绪”或一次历史成功不能复用为本次成功。上述证据仍需配合同场景画面核对。

## 真实游戏实测登记

以下由主测试负责人在实际运行后填写。受控测试不能替代这些项目；RTX40 的硬件项目由用户完成。

| 项目 | 待核对范围 | 状态 | 本次证据／结论 |
| --- | --- | --- | --- |
| Borderlands 3（BL3） | 实际 API、已有模组／原生 DLSS、目标路线、NR 连续处理与同场景画面 | 安装通过；运行待测 | 现有 Luma 的两份冲突模块经预览、备份隔离；DX11 x64 Feeder 安装与文件核验通过。本轮尚未启动验证 NR；DX12 尚未实测。证据 `build/beta3-real-bl3`。 |
| Batman: Arkham Asylum GOTY | 实际 x86 DX9 EXE、系统 D3D9On12 链路、NR、退出与恢复 | 已进 3D；R3 整套安装通过，游戏内待复测 | 已确认旧测试链加载 shim、ReShade、provider 和 x64 宿主 Core，但未取得 NR 完成。由真实游戏暴露的深度解绑和正常 INI 保存维护问题已修复。测试 provider 已归档，完成原配套恢复和 R3 整套重装，检查 ready=true；同场景 NR 回填仍待复测。证据 `build/beta3-real-batman/finalized.json`。 |
| HoYo 正式客户端 | 游戏／渠道／版本、绑定启动器、直接外置加载、当前 NR 与画面 | 绝区零配置及助手就绪通过；游戏内待测 | 国服正式版 EXE 与根 HoYoPlay 绑定；原个人 INI 独立备份，生产 HoYoShade＋DX11 Feeder 首次直接外置安装通过。普通父进程与同用户管理员后台助手完成身份核验并 Ready；未在五分钟内启动匹配游戏，等待超时，未证明 ReShade／Core／NR 在游戏内成功。证据 `build/beta3-real-hoyo`。 |
| Cyberpunk 2077 | 实际 API、所选部署、代理／助手、NR 与原生功能共存 | 本轮未实测 | 不沿用旧版结果宣称本版通过。 |
| Baldur's Gate 3（BG3） | 实际 DX11 EXE、1.4.12 适配桥／1.4.11 回退、NR 与画面 | 本轮未实测 | 适配桥独立选择及回退已做文件事务测试，不能代替游戏运行。 |
| RE 游戏 | 具体游戏／版本／EXE、实际路线、REFramework 等模组共存、NR 与恢复 | 本轮未实测 | 不把引擎识别或单个样本结果推广为全系列兼容。 |
| RTX40 NR | 明确显卡型号、对应 NR 运行库、实际 API 与连续 NR 输出 | 待用户硬件实测 | 未验证 |
| RTX40 MFG／NR 联用 | 已确认原生 FG 的游戏、所选 MFG Unlock 版本、真实呈现倍率、NR 开／关对照 | 待用户硬件实测 | 未验证 |

每条实测至少附上同次启动的时间、EXE／API、显卡、组件版本、活动目录、进程与日志证据、画面观察及恢复结果。MFG 倍率以实际呈现证据为准，配置值或 FPS 上涨不能单独证明倍率。

## 交付检查登记

| 检查 | 当前记录 |
| --- | --- |
| 相关 runtime／catalog／evidence 单元测试 | 22/22 通过；仅对应本子系统 |
| 资源摘要、PE、7 条受控证据一致性 | 通过，指纹如上 |
| 真实资产安装／恢复往返 | 最终 R3 的 DX11/x64 与 DX9/x86：2/2 通过；真实组件完整安装、修改配置、恢复原 INI 字节及逐个源摘要检查。测试目录使用未执行的 PE 身份夹具；证据 `build/beta3-legacy-real-assets.json`。 |
| 全量测试与 UI 检查 | 冻结候选 `1fffc736`：1200 项测试中 1194 通过、0 失败、6 跳过；UI 729 项断言通过，沙箱开启。审查修复另做定向回归。 |
| 实际打包 ASAR 与管理员操作 | 2026-09-10 12:55 UTC 通过。复制候选包，仅增加设置隔离目录的测试入口；生产 main.js 与操作模块字节不变。真实普通父进程安装，两个真实管理员工作进程改键／改回，普通父进程恢复安装前文件，原 INI 完全一致。测试副本 ASAR 与交付 ASAR 分别记录摘要，未冒充相同包；证据 `build/beta3-packaged-uac-report.json`。 |
| 正式审查与定向修复 | 固定 `c66f227c..1fffc736` 做一次运行安全／功能契约并行审查：5 项 P1、1 项 P2 已修复。另由真实恢复发现的宿主 INI 正常保存兼容问题已修。旧配方／恢复／宿主配置联合回归 25/25；MFG 迁移回归 47/47；DX9 R3 两架构 GPU 复测和 MFG 并发门实测通过。审查记录为 `build/beta3-review-runtime.md` 与 `build/beta3-review-functional.md`。 |
| 安装版、便携版、完整 ZIP | 待主发布负责人构建并核验 |
| 最终原文件及压缩包 Defender 扫描 | 待主发布负责人记录摘要、引擎、规则与输出 |
| 26 路线游戏插件手动更新包 | 待主发布负责人记录生成、校验与扫描结果；不是 Manager OTA 导入格式 |

26 个手动路线由 8 个原生组合与 18 个 Feeder 组合构成；HoYo 手动材料限定 x64 DX11／DX12，使用已核验的活动目录。此数量与内部 24 个 Feeder 基础配方不是同一统计口径。管理器接管的安装继续通过管理器更新收据、备份和文件；手动材料中的 `.example` 配置不得直接覆盖个人配置。
