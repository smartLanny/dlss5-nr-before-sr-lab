# Manager 0.4.6 发布核验

Manager 从已发布 `product/xiaofeng-manager-foundation@4efa5f03345fa12d90b209d041eb549893d661ed` 接续，
包含本线程初步 DXGI 修复 `400eee3`。原 hotfix.3 与旧核心、旧附件保留。

## 三个独立选择

- 实际启动 API：DXGI/双 API/只有 EXE DX11 导入的证据不自动启用 DX11 桥接；用户明确指定优先，绑定 EXE，重新扫描不覆盖。
- 核心版本：默认新安装使用 0.4.6；已安装/明确选择的旧版本不被 API 或桥接操作静默替换。
- 兼容桥接：版本选择旁唯一勾选项，默认自动。当前仅 DX11 有对应 carrier；DX12、Vulkan、未知结果不会部署 DX11 carrier。

关闭桥接不修改游戏启动参数，不等同于游戏已切到 DX12。悬浮说明提示用户自行在游戏或启动器中切换。
DX11 关闭后明确报告“桥接已关闭，该 NR 路线不可用”，核心和 nrchain 保留。

## 统一事务与失败处理

已安装游戏勾选并应用，复用 repairGame → installer.repair / upgradeAddon 的配套与完整性检查。
取消并应用，使用同一 file-journal 的 disableCarrier 事务，退役已管理 carrier、移出已知旧 carrier、保留可恢复备份。
明确保存 DX12 同样立即移出 carrier，保留原手动关闭偏好；DX11 其余组件通过安装/修复应用。
文件操作前依旧检查游戏关闭、写权限和反作弊确认；不会覆盖用户改动，也不会用损坏备份回退。
文件事务失败时恢复本次 API/桥接偏好，避免界面假报；事务已成功后的扫描失败不会改报为安装失败。

安装、修复、OTA、版本切换继续采用同一 API/配套规则。手动关闭状态绑定 EXE，并记录在 manifest，直接调用修复也不会重新启用。
ReShade 的精确 DisabledAddons 文件名排除、注册名称提示和原有配置备份规则保留；没有宣称任意手工 DLL 混装或自定义 AddonPath 已得到保护。

## 发布身份

- Manager 产品版本 0.4.6，Windows 文件版本 0.4.6.0。
- 内置中文核心：0.4.6，核心 PE 0.4.6.0。实际编译源码固定 `9087a9efbc7bb53a3c79e7766a174534f49c412c`，不是文档 HEAD。
- 核心 SHA256 `b68f2709a131c9ce0513b6366dbcc2e7d551bef5bcd41934075407378a48c090`。
- carrier 仍名为 `dlss5-native-carrier-045-dx11-compat.addon64`，SHA256 `8268ba3a9d7614ca0e0efad22f7c477780547224dfd0847a1d67188fc05f13c0`。
- 共用 nrchain SHA256 `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb`。
- 中文 OTA：688999 bytes，7 文件，SHA256 `6f633f141a7ba80e2a2d87f440cc9cf1da36233c27b95d73f20445e7f13d9117`。仅一份中文核心，无英文重复 Addon。
- bundle SHA256 `5274ff9100a475c447ff214a7e65a5f8867849305ba90c5052ba8c883fe20163`，默认槽位 0.4.6；旧槽位保持原字节。

`prepare-release-046.js` 在写入前固定验证整套输入，`verify-payload.js --write` 保留新槽并重新校验 pins。
OTA 读取使用显式 0.4.6 身份和角色，不把仍带 045 文件名的 carrier 当成旧核心或任选第一个 Addon。
交流群改为二群 392308850。

## 验证与边界

本地记录：`build/046-final-tests.log`、`build/046-artifact-verification.json`、`build/046-ui-smoke.json`，
界面截图 `build/046-dxgi-before.png`、`build/046-dx12-after.png`。界面运行成品 renderer/preload，游戏数据为隔离夹具。
完整测试包括实际 PR160 工具、API/EXE 保存、损坏载荷拒绝、旧 carrier 备份和事务回滚；实际组件临时目录链另有发布级回归。

核心交接报告为 39 项本地 Windows 编译/WARP 门禁通过。这不代表游戏/NVIDIA Feature18 验收。
本轮没有启动《看门狗：军团》、燕云、2K27 或其他游戏。RR 热切、人物亮度改善及 #153 实际游戏 carrier 崩溃仍需真实游戏确认。
CI 因账户付款/额度未启动，不记通过。公开附件不含原始 F8、玩家截图或游戏日志。


最终本地结果：144/144 全量测试、零跳过、UI contract 通过。两份成品各核对 37 个应用源码文件、完整载荷和 18 组归档版本选择；提取成品代码后 24/24 部署、回滚、UI 与 PR160 交互检查通过。

| Manager 附件 | 字节 | SHA256 |
|---|---:|---|
| DLSS5-Manager-0.4.6-portable.exe | 313947446 | `bc2a93c2faee4fd65c78af7202718d8ab865dd7dce8cf23f64a621de8d6704a1` |
| DLSS5-Manager-Setup-0.4.6.exe | 314284474 | `1d023ef4c84469d344f46d6914117677c780b7a290aa60dbf93683d21ddd7304` |

发布使用既有 beta0.4.6 草稿，核心 tag 固定实际编译源码；不改变旧 beta0.4.5-dx11-compat 附件。
