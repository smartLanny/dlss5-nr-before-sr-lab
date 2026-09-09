# 0.4.7beta Vulkan Provider Core 交接记录

这是一份独立 Core 候选的公开安全交接记录。候选保留 0.4.7beta 的 NR 算法、
界面、PE 身份和默认配置，只在 Core 中加入外部 Provider 消费边界。Feeder 的
Vulkan 传输实现、运行库和模型载荷不属于本 Core 变更。

## 变更范围

- 增加四个版本化导出：`NRExternalProvider_QueryV1`、
  `NRExternalProvider_ClaimOwnershipV1`、
  `NRExternalProvider_ReleaseOwnershipV1`、
  `NRExternalProvider_SubmitFrameV1`。
- 外部 D3D12/ReShade 对象身份解析经过 canonical `IUnknown` 解包；无法证明同一
  设备时保持旁路。
- 0.4.7beta 的默认配置保持不变：`Intensity=1.2`、`ColorStrength=1.0`。
  该 Provider Core 没有新增配置键（manifest 的 `new_config_keys=[]`）。

源与载体身份如下：

| 项目 | 身份 |
| --- | --- |
| 047 Core 算法/UI 基线 | `6a0ad7684683993328a7a98a9bea83ac76b01a64` |
| 本次交接源提交 | `4ecc6d02ca6058cb1ebb0faae5aa49b34b0614ad` |
| DX11 carrier 载体 | `28aed4099b0fe1c207b20b5fee5364c0773c25c2` |
| Core 版本 | `0.4.7beta` |

## 可复现构建

以下命令使用干净的 carrier checkout 和本地 LLVM-MinGW/ReShade 源码目录；尖括号
内容是调用者自己的路径，不应写入公开报告：

```powershell
python scripts/prepare-vulkan-047-core.py `
  --carrier <clean-carrier-checkout-at-28aed409> `
  --output build/vulkan-provider-047-final
Set-Location build/vulkan-provider-047-final
pwsh -NoProfile -File build-external.ps1 -Clean `
  -ToolchainRoot <llvm-mingw-root> `
  -ReShadeSourceRoot <reshade-source-root>
```

生成 gate、双语链接和 PE export table 检查均通过。产物摘要：

| 产物 | SHA-256 |
| --- | --- |
| 中文 Core | `93011d9283615ea9dc8e92955f5ca6aeff01435925f63e941dc1eea1128a372c` |
| 英文 Core | `b794349f360432f072713aba08645613dbbb0d56c112520eab50c2e475e21b55` |
| `nrchain_nvngx.dll` | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |
| 生成的 `provider.cpp` | `e38d0f2ca5b968ccd3a48084b22d4c5ae73cbcfd9ff4dce9b8d69b6dfdbf7d19` |
| 外部构建脚本 | `0c18c19d31cd64098452bd2057fe8cb7a4848dfd6daa49666604bf05e7ea4bf1` |

生产 prepare gate 也已通过：recipe `nr-vulkan-047-20260909`、Core version
`0.4.7beta`、source `4ecc6d0`、22 个资产、总计 168384735 bytes。该 gate 只证明
载荷清单和身份检查通过，不改变 Core 的游戏验收边界。

## 受控 GPU 结果

证据标签为 `vulkan-047-20260909-r2`。在普通权限下运行受控 Vulkan 场景，使用
RTX 5090 Laptop GPU 完成 600 帧：NR 处理、同帧回传和输出变化均被验收；进程以
exit 0 正常结束。场景摘要为 600 presents、1800 draw calls、2 次深度回读，
`depth_motion_verified=1`。HKCU 与 HKLM 注册表快照在结束后精确恢复。

证据只以摘要 digest 交接，原始日志和机器路径不进入源码：

| 证据成员 | SHA-256 |
| --- | --- |
| `result.json` | `5385bd3dacf715cf8dc071de0eab944244c7076eaab8dfb6e00373e12d25368c` |
| `game/report.txt` | `805bde58fc1c61078c451b8eb05365642f551817c5cf67b5459b63a2e4261e96` |
| `registry-all-before.json` | `39a4fc019fee8e411078be8ea95768777eaf72ead28c6c9a992497fc1ba25892` |
| `registry-all-after.json` | `39a4fc019fee8e411078be8ea95768777eaf72ead28c6c9a992497fc1ba25892` |

## 验收边界

这次结果证明的是受控 Vulkan GPU 链路在该 Core/Feeder 组合下完成了规定的
600 帧传输和恢复合同。它不证明任意真实游戏的启动、API 兼容、画质、长期稳定性、
性能或 NVIDIA 模型行为；真实游戏验收仍由维护者另行执行。Core 交接不自动进入
Manager OTA、main 或稳定发布线。

本文不包含绝对用户路径、进程编号、原始日志、进程地址、第三方运行库、模型或其他
供应商载荷；这些内容继续留在受控证据位置。
