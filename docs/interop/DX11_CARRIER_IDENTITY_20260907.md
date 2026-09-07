# DX11 配套名识别（#153 旁路，非闪退修复）

原始候选基线 `ba71b45`；Windows 验证见下。**这不是 BG3 闪退修复。** 云端未编 Windows OTA，未读到 issue-153 大包（仅 ROG）。

## 闪退（候选假设，不是根因结论）

`multiapi-dev3` carrier 闪退、`r3-exp2` 可进。源码对照确认 dev3 完成补丁去掉了 NGX/创建设备的访问异常转换；这使“未限定的 NGX hook 触发硬件 fault 后由异常外逸导致进程退出”成为可证伪候选，但没有转储、异常模块和调用位置，不能把它写成已定位根因，也不能据此恢复 SEH。

启动路径本身是旧版继承行为：两版都会在 `DllMain` 中调用 `PreferWorkingNgxLoader`、注册 `StartWatchingForNgx`，并由 `HookNewNgxModules` 扫描可见 NGX 导出；新 carrier 的无 device/present export-hook 实验已证实这条副作用。该事实不能单独证明具体游戏的硬件 fault 位置，也不把 `r3-exp2` 标成当前配套。

## 本 PR 实际改的

源码 `MatchingCarrier` 仍只认 `045-exp3`，已发布文件名是 `dlss5-native-carrier-multiapi-dev3.addon64`，DX11-compat 打包名是 `045-dx11-compat`。这只影响 F8 `carrierLog` 与 UI「是否观察到本版配套桥」，**不会单独导致闪退**（`NrDx11DeploymentBlocked` 不因 !matching 拒绝）。

现在源码同时认这三个已发布桶名，仍拒绝 `r3-exp2` / `dev1` / `dev2`。打包脚本把整段改写成单一当前 `CARRIER_NAME`。

## Windows 与 staging 验证

最终本地 staging 使用固定上游 `28aed4099b0fe1c207b20b5fee5364c0773c25c2`，输出的身份为：

- `source_commit=4b6f006a7c8e5daee1b9cf5899e4630f4c727f67`（生成器记录当时文档 head；实现代码提交仍是 `48908ad96c407151331a2e818ca30ae1d178b230`）。
- `carrier_commit=28aed4099b0fe1c207b20b5fee5364c0773c25c2`。
- `integration.carrier_filename=dlss5-native-carrier-045-dx11-compat.addon64`，生成源中三个已发布名只出现一次，`mirror_rect.cpp` Windows 编译和运行通过。
- staging `source-manifest.json` SHA-256：`D3D0D41CC04EB591B90420A99D2495E50C49820E9D12450178622D1A07988F9A`。

实际执行的本地门禁结果：

- `python -m unittest discover -s tests/native_bridge -p test_prepare.py -v`：6 项通过。
- `python -m unittest discover -s tests/native_bridge -p test_045.py -v`：5 项通过；1 项跳过（`test_actual_pinned_carrier_patch` 需要显式 staged carrier 环境）。
- 设置最终 staged carrier 与 LLVM-MinGW `CXX` 后，`python -m unittest discover -s tests/native_bridge -p test_carrier_completion.py -v`：4 项通过，包含 35 项生产退役断言、WARP fence completion/timeout 和异常外逸子进程。
- `python -m unittest discover -s tests/multi_api -p test_candidate_packaging.py -v`：4 项通过。
- staging 的 `mirror_rect.cpp` 实际输出：`1097 production rectangle/copy/deployment checks passed; no GPU/game claim`，随后 `mirror_rect=PASS`。

这些是源码、生成身份、Windows 编译和受控 WARP/函数门禁证据；没有 NVIDIA Feature18、真实硬件、游戏运行或实际 F8 `carrierLog.status=matched-loaded-path` 结果，也没有提交 raw/game logs 或二进制包。因此 source/name 唯一性不能推成游戏中已经匹配成功。

## 验收

闪退仍待 ROG 用已发布 `multiapi-dev3` 或后续包，并保留崩溃转储/carrier 日志。本补丁只要求 F8 `carrierLog.status=matched-loaded-path` 在装了 multiapi-dev3 或 dx11-compat 桥时不再误报 missing。

Refs #153, #80。
