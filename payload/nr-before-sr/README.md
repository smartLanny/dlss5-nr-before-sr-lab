# 装机宅版 Release payload

发行包使用紧凑结构：每个显卡族只保留一套固定运行组件，每个可切换版本只保留 addon 和配置。

目录结构如下；发行包默认选择 `versions/0.3.3.5`，其他版本用于用户手动切换测试：

- `fixed/RTX40`、`fixed/RTX50` — 对应显卡族的 `ReShade64.dll`、`nrchain_nvngx.dll`、`nvngx_dlssnr.dll`
- `versions/0.2.0-beta.2`、`versions/0.3.3.5`、`versions/0.4.1-r2`、`versions/0.4.4test`、`versions/0.4.5-ota`、DX11 兼容槽 — 各版本的 addon 和 `nr_before_sr.ini`；界面将 0.4 系列统一显示为 `Beta0.4.x`，内部目录 ID 保持不变。`0.4.5-ota` 是标准 OTA 更新槽，DX11 兼容槽额外包含 native carrier addon

0.4.x 的 `nrchain_nvngx.dll` 是放在 `fixed/RTX40` 或 `fixed/RTX50` 的共用固定组件，覆盖 RTX 20/30/40 的 RTX40 组件和 RTX50 组件各一份。切换 0.4 版本主要替换 addon；导入标准 OTA 时会校验包内 chain，但默认不覆盖游戏目录里已经正常工作的 chain，以免升级破坏现有环境。若诊断发现 chain 缺失或校验不一致，使用对应版本的“修复”会重新补齐。

这些文件必须是**合法取得且相互匹配的 x64 文件**。如果重新准备 payload：

- 固定运行文件放入对应的 `fixed/<family>` 目录；
- 版本 addon 和配置放入对应的 `versions/<version>` 目录；DX11 兼容槽还要放入 `dlss5-native-carrier-exp1.addon64`。

Then run:

```powershell
node scripts/verify-payload.js --write
```

脚本会写入带 SHA-256 校验值的 `bundle.json`。缺失或校验不匹配时，应用会拒绝安装。
除非单独确认了再分发权利，否则不要把这些二进制提交到公开仓库。
