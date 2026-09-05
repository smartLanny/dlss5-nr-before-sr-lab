# DX11 r3 exp2 — 实验候选，不是已验证兼容版

## 基线与用途

源分支：`codex/dx11-r3-render-control`，从 canonical `main@49dd9d4a619a6e73a89ad9d37e3a92a031c57d8d` 开始。其运行时代码与 Release `beta0.4.3r3@d42a8eb23778bc602d0d638193872d41216c2203` 一致；不包含 0.5 高强度算法，也不整树合并旧分支。面向 `next/0.5` 的独立 Draft，**不自动合入 main/release/0.4，不创建 Release/OTA**。

这是 #46 的 r3 后继试验。#46 的 Windows 编译成功不等于游戏成功；#58 的 BG3 DX11 报告明确为旧 exp1，SR Create 成功而 Feature18-success=0，命令状态原因 `null-pso-policy-disabled`。普通 D3D12 r3 的 #67 未直接修复那份旧桥二进制。

## 这次改变什么

1. 使用完整 r3 核心/NR 工作比例/反馈/颜色与历史保护，替代旧 0.4.2 核心。固定 NIGos bridge `29baa4cc56e0aeb21219de16528e697b91522efc`，实际编译经过审查的生成源码，不仅声明接口。
2. 仅在桥**私有** D3D12 Evaluate 入口绑定一个已知空计算 PSO 与空 root signature。没有额外 Dispatch，不改游戏任意 command list；通过 ReShade wrapper 记录状态，交给 r3 原有状态恢复验证。`ExperimentalNullPsoCarrier=0` 不变。创建失败/换设备失败时维持原生回退，不伪造 shadow state。
3. 保留 #46 的同帧回填判断、跳过原生 SR 后的历史 Reset/恢复、真实 depth/exposure 检查、共享 fence 和转换 MV 不被原样 CopyResource 覆盖的修正。
4. 新 UI `游戏渲染分辨率（实验）`：50–100% **线性尺寸**请求，默认 0=游戏控制。观察 NGX capability/get-parameters 中的 `DLSSOptimalSettingsCallback`，在厂商成功返回且真实 min/max 接受时，仅修改 `OutWidth/OutHeight` 建议；不改 query 的输出 Width/Height、preset、DRS 上下界、Evaluate、纹理、jitter、FG 或 swapchain。
5. UI/日志分别显示保存请求、查询是否接受、核心最近报告的 R/O（可能为历史值）、NR 成功与效果提交。**没有“保存成功=已生效”的绿色承诺**。

## 为什么两个分辨率控件不一样

- 现有 NR 比例：插件自己拥有 N 尺寸资源，r3 可直接控制 N/R。
- 新真实渲染比例：请求游戏自己改变 R/O。必须让引擎重新查询并分配/使用低分辨率 SceneColor，再用 SR 输出 O；不能事后把成品缩小冒充节省游戏渲染。
- 50% 线性尺寸约为 25% 像素，但绝不是 FPS 4 倍保证。CPU、几何、复制、NR/SR 固定成本都要测。
- 改请求后尝试切换游戏 DLSS 档位或显示分辨率触发查询。一些游戏缓存参数/回调，重新进入游戏或重启也不保证会经本钩子；UI 无 callback 时必须报未生效。没有公开参数入口的静态 SDK、外部私有回调、仅固定 DLAA 的 Luma/TAA 模组不能因此获得通用尺寸接管。
- 游戏 DRS 仍拥有最终决定，未改 min/max。超出运行库当前 preset 的范围时保留原答案，不偷偷切换模型/质量标记。
- F6 关闭 NR 并暂停后续尺寸覆盖，但**不能立即恢复引擎已分配尺寸**。点击“下次查询恢复游戏控制”并触发重查，才恢复游戏尺寸政策。

## 输入范围与更多游戏路线

当前可尝试：原生 DX11 DLSS；或者已有 Luma/专用 TAA→DLSS 模组发出标准 D3D11 Feature1 的游戏。BG3 DX11、Skyrim SE 的一套既有 DLSS 模组、Prey/Luma 是候选，不是已确认支持列表。先单独验证上游提供者和桥，再加本 NR 核心。

FSR2+/XeSS→DLSS 外部提供者也只是候选；其私有回调/不同资源合同和 hook 竞争不得一概宣称兼容。后续独立原生 provider 继续 #14。

Feeder/synthetic/OFA、Vulkan、OpenGL、32-bit host 本轮**未启用**，旧桥 native-only 限制保留。不能把 Feeder 的小工作图当真实 R 控制，也不能把 Luma generic UE 的 DLAA 当所有 UE 已有 SR。已有两个 NR addon 同时存在必须先排除。

FG/MFG 本轮**不接管**。#16 单独验证 Streamline/外部后端能力、Reflex、HUDless 和帧节奏；#63 的模块观察缺口也不通过删除 RR/生命周期保护解决。不能用显示帧率代替源帧率。

## 生成、构建、产物身份

正常发布 `scripts/build.ps1` 不变。实验入口：

```powershell
python scripts/prepare-native-bridge-r3.py --upstream <pinned-carrier-checkout> --output build/native-r3
./build/native-r3/core/scripts/build.ps1 -ToolchainRoot <llvm-mingw> -ReShadeSourceRoot <reshade>
```

详细可复现构建见 `.github/workflows/native-bridge-r3.yml`。保留的 `prepare-native-bridge.py` 是 #46 的固定变换库/负对照，**不要用其 0.4.2 入口构建本实验**。关键 r3/上游 blob、精确唯一补丁锚点、干净已提交源、完整生成源 SHA256 都会检查；不在游戏目录补丁，不下载模型。

实验核心显示 `beta0.4.3r3-dx11-exp2`；生成源报告 `sourceDirty=1` 是显式生成树，真实源 commit 和完整变换文件 hashes 在 `source-manifest.json`。不要删除此标记假装官方签名版。产物不含 NVIDIA DLSS/NR runtime、模型、游戏资产。

### 安装试验

仅离线单机、允许 mod 的环境。保存原有配置和能工作的旧包；退出游戏后操作。使用现有正确 DX11 ReShade Full Add-on 安装，**不要把 DX11 游戏切到 DX12 来测试此路径**。

选择且只选择一种语言的实验核心 addon，同时放置 `dlss5-native-carrier-r3-exp2.addon64` 与配套 `nrchain_nvngx.dll`，使用同一可写 addon 目录；不要同时加载旧核心、两个语言核心、旧桥或其他 NR 所有者。游戏原有 DLSS 和合法 NR 运行库需要自行保留/提供。首次不要覆写已有 `nr_before_sr.ini`；备份后显式选择实验配置。第一次以游戏原生 R、N=100% 试验，RenderScalePercent=0，确认实际 NR 提交后再分别降低 N 和请求 R。

## 验收表（尚不能勾选游戏通过）

- [ ] 桥单独 / 核心 NR 关 / NR 开，同一源场景和 R/O。
- [ ] #58 BG3 连续 Feature18 success、transfer bound、exact-fence submitted/reclaimed 增长；不能只看 SR Create 或一次 callback。
- [ ] 同一场景 F6 截图有可复现 NR 差异；identity 丢弃画面选项关闭。
- [ ] 真实 R 控制：记录 query、实际 SR 输入 active rect、Color/Depth/MV extent 与 viewport、输出 O；区分 padded allocation、DRS、历史数值和其他视口。查询成功本身不是这个验收。
- [ ] N=100/75/50%，R 采用引擎可接受多个比例；HDR/曝光/skin/UI、camera cuts、读档、切窗、10,000 源帧和回退。
- [ ] 测 CPU/GPU bridge/NR/SR/总帧时与源帧 1% low；不把 MFG 显示 FPS 当基础性能收益。
- [ ] 原生 D3D12、RR 旁路、FG 关闭/已有 FG 共存、未知 hooks、缺少指南数据、设备/分辨率重建，无回归。

## 验证等级

源码审查/CPU 策略测试/真实生成源编译与 WARP private-baseline smoke 由 CI 提供。WARP smoke 只创建/绑定/重置实际 helper，不执行 NGX，不证明 ReShade hook 状态恢复或游戏视觉效果。最终 CI 状态以关联 PR 为准；**此文档不宣称已经 compile/CI/runtime verified**。游戏与 NVIDIA Feature18 运行时验收必须另附记录。

## Issues 状态调整原则

#4/#7：NRScale 已在 r3 有运行代码，不应仍作为未实现元数据；真 R 控制独立验收。
#13：新尺寸控件在同一核心 Overlay，不整树合并旧 Quickbar/Manager。
#14：外部 SR 提供者试验不等于原生 FSR/XeSS adapter 完工。
#58：本分支为 targeted candidate，保持 open/需复测，不自动 Fixes/close。
#38/#50/#64 的标准 DX12 null-state、#65 unregistered-list、#68 exposure/观察缺口、#63 FG 环境 hook 失败必须分流，不都算 DX11 修复。
#16：FG/MFG 控制与未知 hook 共存继续独立，不能通过 UI 倍数选择冒充 40 系 MFG 已成功。

## 一手参考

- NVIDIA helper 合同（读取 callback → vendor result → OutWidth/OutHeight 和动态边界）：https://github.com/NVIDIA/DLSS/blob/a291cc7d2cc642a51566f3dfd5376f635cd1b284/include/nvsdk_ngx_helpers.h
- NIGos 固定源码（保留 MIT notice）：https://github.com/NIGos/dlss5-bridge/tree/29baa4cc56e0aeb21219de16528e697b91522efc
- 旧桥实现与验证边界：本仓库 #46；用户失败证据：#58、`bug-inbox/issue-58/20260905-neroeggs-bridge-excerpt.md`。
