# DX11 配套名识别（#153 旁路，非闪退修复）

基于 `origin/main@ba71b45`。**这不是 BG3 闪退修复。** 云端未编 Windows OTA，未读到 issue-153 大包（仅 ROG）。

## 闪退（候选假设，不是根因结论）

`multiapi-dev3` carrier 闪退、`r3-exp2` 可进。源码对照确认 dev3 完成补丁去掉了 NGX/创建设备的访问异常转换；这使“未限定的 NGX hook 触发硬件 fault 后由异常外逸导致进程退出”成为可证伪候选，但没有转储、异常模块和调用位置，不能把它写成已定位根因，也不能据此恢复 SEH。

启动路径本身是旧版继承行为：两版都会在 `DllMain` 中调用 `PreferWorkingNgxLoader`、注册 `StartWatchingForNgx`，并由 `HookNewNgxModules` 扫描可见 NGX 导出；新 carrier 的无 device/present export-hook 实验已证实这条副作用。该事实不能单独证明具体游戏的硬件 fault 位置，也不把 `r3-exp2` 标成当前配套。

## 本 PR 实际改的

源码 `MatchingCarrier` 仍只认 `045-exp3`，已发布文件名是 `dlss5-native-carrier-multiapi-dev3.addon64`，DX11-compat 打包名是 `045-dx11-compat`。这只影响 F8 `carrierLog` 与 UI「是否观察到本版配套桥」，**不会单独导致闪退**（`NrDx11DeploymentBlocked` 不因 !matching 拒绝）。

现在源码同时认这三个已发布桶名，仍拒绝 `r3-exp2` / `dev1` / `dev2`。打包脚本把整段改写成单一当前 `CARRIER_NAME`。

## 验收

闪退仍待 ROG 用已发布 `multiapi-dev3` 或后续包，并保留崩溃转储/carrier 日志。本补丁只要求 F8 `carrierLog.status=matched-loaded-path` 在装了 multiapi-dev3 或 dx11-compat 桥时不再误报 missing。

Refs #153, #80。
