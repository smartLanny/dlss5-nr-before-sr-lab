# 0.4.5 Multi-API integration candidate

## Scope / 实际交付边界

本分支合并 #131 (`f88adc33`) 与 #132 (`2ab31a17`) 的真实 ancestry，并吸收前次 MultiAPI-Beta-Foundation 源码实验。合并提交双父 `45ba2c91` 仅推进隔离实验分支，未推进 main/release、未移动 tag，也没有重复把 F8 改回 ZIP+TXT。

**已接线的扩展**：当前 0.4.5-fix1 NR 核心、DX11 native carrier 的有效区域/偏移回填/按句柄 flags、2K25 相关成功 Reset 漏记修复、F8 单个 TXT、Bridge 1.4.12 固定构建、前置 FP16/FP32 typed/typeless 读取到既有 FP16 proxy/合成路径。可选模块枚举失败不再独立阻止 NR；正向重复 NR/多桥证据和实际资源/命令/队列保护不变。

**未交付**：Vulkan 原生中途提交传输、通用 Synthetic/Legacy consumer、RR→NR 实际执行、后置低分工作图、FG/MFG 倍数控制、GetDesc spoof。不能将构建中带有这些上游文件或普通 UI 字段当作运行实现。

BG3 旧 DX11 beta 的主人验收通过保留，#58 不重开；没有本组合游戏结果，不将旧通过绑定到新 SHA。#127 的 2K25 修复仍为相关候选，不把创建证据或新 F8 完成当作游戏已修。

## 运行路径

```
原生 D3D12 Feature1 ─────────────────┐
DX11 原生或已有模组 Feature1 → bridge ┤→ 同一个 NR Core → 原生/私有 SR
                                   ┘
```

引擎 TAA 提供者只要给出可用标准 DLSS 调用就是候选入口，不宣称所有 Luma/UE/Skyrim/奥德赛组合已支持。没有 DLSS 调用的游戏仍需后续 Synthetic/Extracted provider，而不是更改旁路文字。

## 格式规则

`NrResolveSceneSrv` 在实际设备上查询合法 typed format 的 Texture2D/ShaderLoad 能力。FP16/FP32 TYPELESS 的 FLOAT 解释有明确日志；物理格式与 SRV 格式分别保存。失败不会尝试数值不等价的 UINT/UNORM，也不会改写 COM 的 GetDesc。

前置源只读，不要求原图具备 UAV：NR/合成使用已有自有 FP16 工作纹理。FP32→FP16 可能改变范围/精度，需要画质验证。枚举同族格式的通用 policy 不代表每一种都接进完整生产 shader；本轮没有未验证的整数色彩或任意后置回填转换。

D3D12 CreateShaderResourceView 返回 void，不能“返回成功就认为合法”；创建前验证资源/视图配对和实际能力。新 WARP/SDK 验证独立于主机测试；未知格式语义不通过盲试制造 GPU 故障。

## 为什么 native Vulkan 仍不能只开 vk_mirror

固定上游 1.4.12 修正了 Vulkan R16 曝光及不同输入/输出格式；这里固定其源码而非宣称本组合 Vulkan 通过。其 vkmirror 在游戏尚未提交的 VkCommandBuffer 中使用 host event park，源码明确记录 VUID-vkSetEvent-event-09543 等规范问题。合法替代需要正确的提交边界/原生 temporal 数据生命周期。Feeder 的 ReShade 后处理 flush/signal/wait 可作为另一条插入边界，不等价于已解决原生 DLSS 中途插入。

## 合并与构建

```
#131 ─┐
      ├─ isolated integration → 本 PR → release/0.4.5-test → release/0.4
#132 ─┘                                                    → main → next/0.5
```

实验唯一构建入口：

```powershell
python scripts/prepare-multi-api-beta.py --upstream <bridge-v1.4.12-checkout> --output build/multi-api-beta
./build/multi-api-beta/core/scripts/build.ps1 -ToolchainRoot <llvm-mingw> -ReShadeSourceRoot <reshade>
```

生成器验证两个父分支均为祖先、当前源码 blob、固定上游 SHA、完整生成文件 SHA256。所有末尾身份改写与 manifest 校验也在临时目录内完成；失败不留下可误用的半成品输出。不重置用户工作区。正常发布入口仍与实验构建隔离。

#132 的成功原生 Reset 观察/F8 单 TXT 原文随合并保留，生成器不替换它。源变动须审查后重新固定 pin，不能用更宽泛的正则跳过失败。

## 测试层级与矩阵

主机已执行：GCC/Clang 各 12,362 条格式策略断言、各33条实际 adapter CPU 接缝检查，Clang sanitizer；Python最小补丁和事务夹具。完整源码补丁、Windows双语核心/桥、WARP和游戏必须另行记录，不能继承旧绿色 CI。

| 分组 | 验收 | 当前 |
|---|---|---|
| BG3 DX11 | 新组合回归、预览/读档、R/N、fence回收 | 旧beta通过；本组合待验 |
| 非原生DLSS的已有TAA模组 | provider单独、NR关/开、native guides、偏移/格式 | 待验 |
| FP32/typeless与无源UAV | 真实typed SRV读数、精度、NR→SR | host策略通过；GPU/游戏待验 |
| NBA2K25 | 真实Reset恢复、F8一个TXT、NR提交 | 相关修复合入候选，待验 |
| Native Vulkan | 合法submit边界、同帧copy-in/out | 未实现 |
| Feeder/Synthetic + DX9/10/GL/x86 | 唯一consumer、协议版本、结果回填、各API transport | 未集成 |
| RR→NR | RR成功输出后置、guide/相位、100%再低分、恢复用户请求 | 未实现 |
| FG/MFG | NR关/开×FG关/开，官方与外部后端分开 | 本轮无倍数控制 |

测试包只有完整当前构建通过才产生。一个语言核心 + 同提交nrchain + 配套multiapi carrier；默认INI只在defaults/，不覆盖旧配置、不带NVIDIA运行库/模型。保留已成功包到游戏目录外。首次R游戏控制、N=100%，确认NR/提交与F6画面，再分别测试N与R。

旁路必须显示具体原因并提示F8生成TXT、上传BUG；图像问题按需F7。F6只关NR，不立即回滚引擎已分配R。持续异常停止回退。任何网络游戏测试不得绕过反作弊或保护。

## 余下可独立合并的工作

优先实现 Feeder消费者 ABI/结果回填与 RR成功输出提供者，两者不复制NR核心。Vulkan原生中途同步单独开发；Legacy只通过版本化外部transport接入。真实 R、NR工作比例和FG控制独立记录请求/已应用/原因，不添加假开关。

参考：#2 #4 #15 #47 #79 #80 #94 #95 #104 #124 #127 #131 #132；NIGos/dlss5-bridge@28aed4099b0fe1c207b20b5fee5364c0773c25c2。保留所有上游 MIT notices，不复制未许可第三方源码。
