# 无原生 DLSS 的 DX12 Feeder 候选

本候选让没有原生 DLSS 的 x64 DX12 游戏通过独立的 ReShade Feeder 向现有 0.4.7beta Core 提交成品帧，并在同帧录制 NR 输出回填。Core 保持原二进制；Feeder 不再持有第二套 NR Feature。导引标记为 `Synthetic`，不是引擎原生运动矢量。原生安装的 `ERR_NO_DLSS` 限制保留，SR 与 FG 仍分别依据游戏真实 x64 组件判断；此配套不注入 SR、FG 或 MFGUnlock。

当前准入为 RTX 50、x64、DX12。运行时还必须观察到真实 swapchain 的 `R8G8B8A8_UNORM` 与 `srgb_nonlinear`，并保持原尺寸输入、`work_resolution=100`、`work_upscale=0`、Core `R8OutputEncoding=2`。HDR、BGRA、未知颜色域、DX11、Vulkan、x86 未纳入此候选。未确认颜色域不能通过填写配置绕过；运行时拒绝时保留原帧。

管理器提供独立的 Feeder 准备、检查、恢复入口。真实游戏 EXE 旁只放固定 `dxgi.dll` 与新 `ReShade.ini`，其余 22 个文件置于 `_DLSS5_Feeder/`。安装收据位于游戏根目录 `_DLSS5_Backup/xiaofeng-feeder.json`，写入经过文件事务保护。未知加载器、已有 ReShade 配置、其他路线绑定、第二运行组件、不可确认的普通权限启动、路径过长和运行中的游戏会在部署/启动前拒绝。相同摘要的现有加载器可复用，恢复时保留；用户修改的受管配置归档到 `_DLSS5_Backup/feeder-settings/<installId>/`。另一原生/Vulkan路线的中断事务必须由原路线恢复。

文件准备、当前进程已加载、当前进程已完成 NR 三种状态分别报告。日志读取仅访问所选 EXE 的受管日志，限制大小，核对启动时间与 PID；文件准备完成不等于运行效果验证。真实游戏未验收，因此 `runtimeVerified=false` 和 recipe 的 `realGameVerified=false` 保留。

| 固定身份 | SHA-256 / 指纹 |
| --- | --- |
| 当前 recipe，24 文件、173,863,033 字节 | `734eb01140021930234c0c7ce7bb7472abad73f177a5b8fc582a95e67a6da778` |
| 未改动的 0.4.7beta Core | `93011d9283615ea9dc8e92955f5ca6aeff01435925f63e941dc1eea1128a372c` |
| DX12 Feeder Provider | `05f6c6d720378c05ff49332498cf065682806e4742cef473f150e0b844c12aa7` |
| nrchain | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |
| NVIDIA NR runtime | `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e` |
| ReShade 6.8.0 x64 loader | `0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7` |
| 受控 DX12 宿主 r4 | `8ad0d8e9528396a2aa8998e03bec8c91b3cf5402c5741eb819a5b6dab8490f49` |
| 宿主源文件 | `5b47cc17d91bc4f159d3944670dff5967700961a436837af395da8af499d47c9` |

被实际测试的旧 recipe 指纹为 `b16251e52657fdefed0a7c84505febadde838b98ddc8a406118fad4f24d38954`。本次只更新验收范围元数据，24 个组件字节未变。管理器代码只允许当前指纹作为安装来源；旧指纹仅在明确白名单中允许验证历史收据并恢复。更新既有候选时需先恢复旧配套。

2026-09-09 的实测使用 RTX 5090 Laptop，真实 D3D12 场景、三组移动几何图形与实际 raster depth。宿主通过普通游戏 Present 触发 ReShade/Feeder，不自行 Claim/Submit/Create/Evaluate NR；`QueryV1` 仅只读观察 Core 回填计数。所有结果来自受控场景，不是商业游戏或画质评测。

| 运行 | 已确认结果 | 边界 |
| --- | --- | --- |
| NR-on，640×360，600 帧 | Core Feature 18 Evaluate 与 Feeder completion；10 个采样均有 230,400 个 RGB 像素变化、alpha 未变 | 初次固定尺寸 on/off 没有单独捕获 OS 退出码，不能写成 0 |
| NR-off，640×360，600 帧 | 无 Core Evaluate/Feeder completion；10 个同帧输入 hash 与 on 完全一致 | 每帧仍有 45,260 像素的固定 ReShade 差异；不能称 off 全帧等于原始场景。每个配对样本额外变化下界为 185,140 像素 |
| 早期 resize，180 帧 | 正常退出；resize 后 shader 刚编译完即结束 | 观察窗口不足，不据此判定长期恢复失败 |
| r3，PID 98580，1,200 帧、3,600 draw calls | 普通权限启动；640×360 在第 301 帧改为 768×432；真实回调恢复 NR Evaluate 与回填；持有进程句柄捕获 OS exit 0、无超时 | 只验证一次尺寸切换；未验证反复切换、全屏、设备丢失或商业游戏退出生命周期 |

r3 的 resize 于 `06:11:40.804Z` 开始、`06:11:40.837Z` 完成。旧尺寸第 300 帧只读计数为 293。日志以本机 UTC+8 记录：`14:11:41.000` 旧 Core GPU fence 完成、`.001` Release 成功；`.165` 新 768×432 Feature 创建成功；`.166/.174/.210` 新尺寸 Evaluate 成功。第 360 帧计数为 55，随后每 60 帧增加 60，至第 1,200 帧为 895。**这是释放资源后的新计数 epoch，293 和 55–895 不应合成一条单调累计计数。**

r3 resize 后的 15 个样本均报告 331,776 个 RGB 像素变化、alpha 未变、3 种 depth 值。每个样本的对照是**同一帧 Present 前场景读回和该次 Present 后处理结果读回**，没有拿不同动画帧或 resize 前后图像相减。像素变化本身不全部归因于 NR；它与独立 NR-off 对照、Core Evaluate、真实 Feeder completion、只读回填计数共同支持 NR 已执行。当前默认反向深度配置与宿主深度语义未作画质验收，保留 Synthetic 导引、光照/色调和运动质量的限制。ReShade 退出 reference-count 警告也保留在原始日志中。

冻结证据在 `build/feeder-dx12-resize-r3-evidence/`，包含原始报告、3 份日志、实际配置、安装收据、宿主源码和构建身份；`evidence-identity.json` 为 `808c4c7936c5c73e4d29a5c96ae94c1b1dbff1e712423e3c09bf7017cb5649ec`。联合 on/off/r3 摘要 `build/feeder-dx12-evidence-r3.json` 为 `3dbdb51972e0ade859982bb649aebac8f988955de6b6c8f15305411971e9539d`；同字节副本保存在本目录的 `feeder-dx12-evidence-r3.json`。摘要同时确认 fixed-size callback、单次 resize、配对输入一致和 NR 仅在 on 运行出现；`completeAcceptance=false`。

Provider 来自独立构建目录内固定的上游 `jlrouzies-fr/DLSS5-Feeder` 提交 `26c002d5156d178c2db438327194077c9ad94418`。`scripts/feeder-dx12-stage.py` 在已有外部 Provider 变换之上加 DX12 SDR 边界，先校验基础变换摘要；`feeder-dx12-build.ps1` 校验固定 NGX 依赖并真实编译链接；`prepare-feeder-runtime.js` 只接受被测试的 Provider 摘要，保留许可证并准备新目录。编译产物原始 validation 的 GPU 字段保持构建时的 false；后续真实运行证据在独立 acceptance 元数据中记录，未改写历史构建记录。

CPU 验证：`node --test test/feeder-service.test.js test/feeder-runtime-evidence.test.js test/app-service-vulkan.test.js` 为 29/29 通过；包含真实临时文件安装/恢复/中断事务、来源与目标漂移、历史收据、日志 PID/时间/读取竞争、Feeder 与原生/Vulkan 的应用服务分流。`node scripts/verify-feeder-runtime.js` 校验全部 24 个组件成功。本轮 GPU 运行已结束；复跑 GPU 需要单独协调运行窗口。
