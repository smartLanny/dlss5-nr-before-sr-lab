# 装机宅版 Release payload

发行包使用紧凑结构：每个显卡族只保留一套固定运行组件，每个可切换版本只保留 addon 和配置。

目录结构如下；0.4.6 发行包默认选择 `versions/0.4.6`，其他版本用于用户手动切换测试：

- `fixed/RTX40`、`fixed/RTX50` — 对应显卡族的 `ReShade64.dll`、`nrchain_nvngx.dll`、`nvngx_dlssnr.dll`
- `versions/0.2.0-beta.2`、`versions/0.3.3.5` — 历史版本的 addon 和 `nr_before_sr.ini`
- `versions/0.4.5-ota` — 公共名称固定为 `0.4.5-DX11-兼容增强`，必须同时包含中文核心、`nrchain_nvngx.dll`、`dlss5-native-carrier-045-dx11-compat.addon64` 和默认 INI

历史版本继续使用 `fixed/RTX40` 或 `fixed/RTX50` 中的共用 `nrchain_nvngx.dll`。`0.4.5-ota` 使用自己版本目录内的匹配 chain；安装或修复时会和核心、carrier 成套校验和替换，但已有 `nr_before_sr.ini` 不覆盖。

这些文件必须是**合法取得且相互匹配的 x64 文件**。如果重新准备 payload：

- 固定运行文件放入对应的 `fixed/<family>` 目录；
- 历史版本 addon 和配置放入对应的 `versions/<version>` 目录；`0.4.5-ota` 还必须放入匹配的 chain 和上述精确 carrier 文件名。只使用已验证的 `beta0.4.5-dx11-compat` / PE 0.4.5.104 中文构建，不可用旧二进制改名代替。

兼容范围：已有原生或模组 DLSS 的 DX11-x64 游戏，以及原有 D3D12 SR 与已知硬件深度 RR 路线。不包含无 DLSS/Feeder、Vulkan、DX9/DX10/OpenGL 或 x86 Legacy。

0.4.6 在已验证的 hotfix.3 载荷上追加，旧槽位保留用于回退：

```powershell
node scripts/prepare-release-046.js <已核验的0.4.6中文OTA解包目录> <已核验的默认INI>
```

脚本固定源码 `9087a9efbc7bb53a3c79e7766a174534f49c412c` 及中文核心、nrchain、carrier、INI 哈希；
Manager 产品版本 0.4.6 / Windows 文件版本 0.4.6.0，内置核心 PE 0.4.6.0。
版本目录只含中文核心、默认 INI、nrchain、配套 DX11 carrier。DX12 仅部署核心与 nrchain；
carrier 只在确认 DX11 且兼容桥接勾选时部署，不支持 Vulkan。旧 0.4.5 槽保持原字节。

Then run:

```powershell
node scripts/verify-payload.js --write
```

脚本会写入带 SHA-256 校验值的 `bundle.json`。缺失或校验不匹配时，应用会拒绝安装。
除非单独确认了再分发权利，否则不要把这些二进制提交到公开仓库。
