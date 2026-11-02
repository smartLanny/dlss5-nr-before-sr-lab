# 上游源码复核与可执行增量｜2026-11-03

本轮重新读取 GitHub 主线提交、PR 状态和实际文件 diff。下列是近两周已发生的
变化，**不代表 11 月 3 日当天发布**。本仓对照基线为
`024f9cc32778c1056db364d76d087ee257da6502`，并核对 #341/#345/#346 的整合范围。
这里只记录有具体实现或测试价值的差异；没有导入上游源码或二进制。

## 1. 当前固定来源

| 项目 | 本轮确认 HEAD | 最新提交的实际含义 |
|---|---|---|
| [OptiScaler](https://github.com/optiscaler/OptiScaler/commit/ab379b25d2ed11b19703dfc181b7d2f02b1438af) | `ab379b25` | `misc/IdentifyGpu.cpp` 同时修正 D3D12 探测和 ASIC 查表的 FSR4 INT8Lite 分类：移出 Navi 10，加入 Navi 14，保留 Navi 12；这是上游能力分类修正，不是新的 NR 模型或本仓 NVIDIA 兼容性证明 |
| [wilsjo2 NR fork](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/commit/6125a75b37f7e2a408e31934f4827e5660429203) | `6125a75b` | 合入 #135 的 Unicode/长路径、借用 COM 对象和菜单缓冲边界修复；实际改动为 `e797906f` |
| [RenoDX](https://github.com/clshortfuse/renodx/commit/43c067b755a6a741f6500b2b936570f1462dcfbe) | `43c067b7` | `src/utils/resource_upgrade.hpp` 在替换 OpenGL 绘制目标后恢复 READ framebuffer |
| [Magpie](https://github.com/SAOG0721/Magpie/commit/6940f84a5db5357f29640a79281e604ec034d953) | `6940f84a` | 最新提交属于发布输出路径维护；近期有用的产品源码增量是 `a72c2b70` 的输入/输出 profiler 分离，以及 `4fc74a19` 的 XeSS 输出间隔观测 |

OptiScaler 官方 NR [#1158](https://github.com/optiscaler/OptiScaler/pull/1158) 仍开放，
HEAD 为 `dc5611d62dac84dc657198d2508a2c7385df651d`。不能把 fork 中的 NR 功能标为
主仓已接受，也不能将这些提交变成本仓新版本的游戏验收。

## 2. #167：交换链创建结果与辅助交换链角色

**已合入的确定修复：**
[OptiScaler `6a7b0336`](https://github.com/optiscaler/OptiScaler/commit/6a7b0336922fe2b85bfa04d46349fb1c41ab5776)
将 `hooks/DxgiFactory_Hooks.cpp` 和 `hooks/DxgiFactory_WrappedCalls.cpp` 的九处
创建后检查改为 `SUCCEEDED(result)` 加输出指针有效性检查。正 HRESULT 可以携带
有效交换链，不能只接受 `S_OK`。

**仍开放的实现：**
[#1278 / `7277516b`](https://github.com/optiscaler/OptiScaler/pull/1278/files)
在 `hooks/Dxgi_Hooks.cpp` 识别辅助调用者，在 `hooks/DxgiFactory_Hooks.cpp`
用 Factory 自身的 GUID PrivateData 标记，让其交换链走原生路径。配置解析在
`misc/AuxiliaryModules.h`；默认列出 `SocialClubD3D12Renderer.dll`。上游源码明确
承认它依赖每次创建取得不同 Factory 对象的假设。

本仓已有 `nr_source_frame.hpp` 的 stream/epoch/sequence、Application/Generated
角色、claim/FG 消费和退休规则，不能重新做一套仅按 Present 次数识别源帧的系统。
基线 `src/` 没有同构的 CreateSwapChain 包装入口；不能宣称已找到或修复同一个
HRESULT 缺陷。应在 #167 的实际 Presenter 接入切片加入以下生产入口用例：

- `S_OK + 有效对象`、`DXGI_STATUS_OCCLUDED + 有效对象` 均接管；成功但 NULL、失败
  但非 NULL 均拒绝；原 HRESULT 原样返回。
- 主 Factory 创建 FG 链后，辅助 Factory 创建 0×0 自动尺寸、119×100、1920×1080
  三种链，均不得替换主 Presenter；再销毁辅助链并复用地址。
- 同一 Factory 同时创建主/辅助链，验证角色属于具体链，不能把模块名或尺寸当成
  唯一归属证据；FG 关闭、resize、在途资源退休后仍能恢复原生输出。

补充旁证：[`2e8032de`](https://github.com/optiscaler/OptiScaler/commit/2e8032de09dfbd8d02480b1ebf587b83b5b47748)
只删除《Control Resonant》的 `FGPreserveSwapChain=false` 游戏默认覆写。
它提示后端与交换链保留策略要组合测试，**不支持给本仓所有游戏统一改变该默认值**。

## 3. #164 / #14：输入模块与执行后端分开，按实际能力传语义

[`760ae412`](https://github.com/optiscaler/OptiScaler/commit/760ae4129daced780d2c83b34fb505794a1aea8f)
在 `proxies/FfxApi_Proxy.h` 加 `IsCustomModule` 和 `LoadSdkLibrary`：第三方执行 DLL
即使与游戏 SDK 同名，也不再作为输入模块被递归 Hook。检测依据是已注册自定义后端
的模块对象，而非单凭文件名。

[#1279 / `c0d59f93`](https://github.com/optiscaler/OptiScaler/pull/1279/files) 仍开放：
`upscalers/dlss/DLSSFeature_Dx12.cpp` 只有在实际模块导出
`HelixSR_UpscaleInputVersion` 且版本至少为 1 时，才传原输入 API 和 Reactive Mask
关闭状态。`upscalers/ffx/FFXFeature_Dx12.cpp` 另行保留 HLX FSR4 自己的曝光路径；
目前该分支靠版本名称前缀识别，不能照搬为通用能力模型。

本仓 `nr_before_sr.cpp` 已有 `LoadedNgxModule` 引用保护、目标模块 pin、
`FindNgxHookTarget(operation, target)` 去重和 observer epoch。新增工作应复用这些
身份与生命周期机制，给 direct/external provider 增加：

- 同名、不同 HMODULE 的输入与后端；forwarded export 指向同一函数；卸载重载/地址
  复用；每次 dispatch 只能进入一个本项目 NR owner。
- 能力导出缺失、版本 0/1/未知版本、可选参数拒绝；只对已证实的 capability 写私有键。
- Exposure 由游戏、adapter、backend 各自拥有的三个对照；失败后不得把同名 API、
  同尺寸或成功返回值提升为 Native provenance。

`ab379b25` 同时说明硬件名称分类也会出错。provider/backend 能力需保留实际探测、
运行库身份与调用证据；本轮不把 FSR4 ASIC 表移植成 NR 网络兼容表。

## 4. #164 / #341：补锁顺序与 OpenGL 状态合同

[#1276 / `f803a655`](https://github.com/optiscaler/OptiScaler/pull/1276/files) 仍开放，
实际修改 `LibraryLoad_Hooks.cpp`、`Streamline_Hooks.cpp/.h` 和 `Ntdll_Proxy.h`。
它以 `LdrLockLoaderLock` 在 plugin mutex 前手动取得 loader lock，并让部分回调
try-lock。**本仓不直接移植这套手动 loader-lock 实现。**

可执行核查点是本仓 `nr_fg_streamline.inl`：`Poll` 在 `install_lock` 内解析/安装
导出，`HookGetFunction` 的 PCL marker 安装也会取得该锁。先做可控的双线程重入夹具，
覆盖模块解析、函数工厂回调、并发安装和卸载；验证阻塞环是否可达，再决定是否把解析
移出锁、用短暂发布或延迟安装。源码结构是调查依据，尚无本仓实机死锁复现。

RenoDX [`43c067b7`](https://github.com/clshortfuse/renodx/commit/43c067b755a6a741f6500b2b936570f1462dcfbe)
在 `RewriteRenderTargets` 保存 `READ_FRAMEBUFFER_BINDING`，调用 ReShade 目标绑定后
只恢复 READ。前置 [`2581bd1c`](https://github.com/clshortfuse/renodx/commit/2581bd1c7b361badb37d2e8f86c79f67ae596507)
提供 `src/utils/opengl.hpp` 调度封装。#164 的 OpenGL transport 应在真实入口验证
READ≠DRAW、后续 blit、同线程切换 Context、Context 销毁。此前跨 API fence/command-list
研究继续保留；这项是状态恢复负控，不能用它宣称 OpenGL NR 已接通。

## 5. #167 / #9：实际输出间隔，避免把缺失样本画成平滑曲线

Magpie [`a72c2b70`](https://github.com/SAOG0721/Magpie/commit/a72c2b70ef7b01a5a08e8332ce7c37818e3954d8)
将捕获帧、内容输出和仅提交计数分开。生产 Renderer 排除旧源帧重画、UI/鼠标更新与
失败提交；profiler 折叠图表时不构建输出曲线。

[`4fc74a19`](https://github.com/SAOG0721/Magpie/commit/4fc74a196993c883ce2f01e96e8ed8d81f1490e6)
在 `PresentationIntervals.h` 增加有界 64 样本队列；`Begin/Complete` 使用 epoch、
serial 和真实时间端点，producer 用 try-lock。漏掉回调、提交失败或乱序完成都会断开
连续间隔；`PresentationFrameRate.h` 排除旧代次/暂停前样本。
`XeSSFGPresenter.cpp` 只在该 provider 明确具备观察能力且 profiler 开启时采样，
未知运行库维持“间隔不可用”。

本仓已有 `nr_runtime_timing.inl` 的 epoch 隔离、缺样本/未知标记以及源帧身份；应增加
输出端采样切片，不重新命名现有 NR 局部 GPU 时间为整帧时间。最小验收是：
60 源帧/120 内容帧；倍率相同但只有批次计数时不生成伪时间间隔；serial 丢失、乱序、
失败、暂停/恢复、后端切换不跨断点插值；关闭 profiler 后 producer 无采样工作。
这是本轮最适合在已有基础上做成小 PR 的外部借鉴方向之一。

## 6. #274 / #89：可选画质对照，复用既有研究

wilsjo2 [`cec9985b`](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/commit/cec9985b0f13e9818e7a7caf259a1c34538a7acf)
的 `precompile/dlssnr_quality.hlsli` 实现 4×4 原图亮度引导的 log-RGB 编辑恢复，以及
NR 网格上两种尺度的暗边抬升；`DlssNr_Dx12_Run.cpp` 复用 `passClamp` 放最终滤波结果。
`dlssnr_residual.hlsl` 以当前编辑邻域约束历史，把输入亮度存在 history alpha，
并在 `DlssNr_Dx12_DeferredSr.cpp` 用本帧/前帧 pre-exposure 比值换算历史。
开关默认关闭；来源署名保留在 `Licenses/RenoDX_ATTRIBUTION.txt`。

本仓已有 LightingLock、引导恢复、near-black guard 和
`nr_temporal_runtime.inl` 的 same-Evaluate pre-exposure/source-to-scene 合同。
**不新增一套并行“默认降噪”来重复这些机制。** 下一步在 #274/#89 固定输入夹具中
对照“当前恢复 / 亮度引导 log 编辑 / 只抬升窄暗边”，分别测亮度守恒、无编辑恒等、
宽阴影保留、细线和 alpha；运动实验再加曝光跳变/遮挡显露/invalid history。
复用纹理不等于没有额外 pass，需单独量 GPU 时间与显存，不从 tap 数宣称净收益。

最新安全提交 [`e797906f`](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/commit/e797906f35039370f82583556d6e1b5cbe4c52fa)
还把 `Streamline_Hooks.cpp` 的模块版本读取改为 UTF-16/32,768 字符缓冲、去掉对
借用 factory 对象的多余 Release，并在 `menu_overlay_dx.cpp/menu_overlay_vk.cpp`
检查/动态取得真实缓冲数量及有界 `VK_INCOMPLETE` 重试。这些应作为未来自有
Presenter/UI backend 的输入边界用例，不替换当前 ReShade 管理的 overlay。

## 交付与不重复项

- 本轮性能代码是本仓自己的 root-argument snapshot 优化，见
  [CPU 验证](../validation/COMMAND_SNAPSHOT_COPY_20261103.md)；没有复制上述上游实现。
- 标量 setter 惰性清理、四槽 TLS 缓存、分槽锁、模块 pin/epoch、源帧角色、曝光合同
  均已有，继续补边界和实测，不重复立项。
- #341/#345/#346 的退休、Continuous SDR、分层网络整合不由外部研究另起一条“最新”线。
- 上述新增夹具/adapter 建议尚未实现，不等于已修复 #164/#167 或完成 GPU/NGX/游戏验收。
  任何后续源码复用都需固定文件版本、保留对应许可/署名，二进制分发单独核实。
