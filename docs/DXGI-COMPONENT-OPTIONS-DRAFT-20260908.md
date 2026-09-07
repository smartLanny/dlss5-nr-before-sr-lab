# DXGI 与独立组件选择：未发布初步候选

基线为 Manager hotfix.3 / `4efa5f03345fa12d90b209d041eb549893d661ed`。
本轮不构建或发布安装包，不替换内置核心，等待后续重大 Release 的明确组件合同。

## 看门狗：军团截图类输入

用户描述的 `DirectX (DXGI) / 64位` 只能说明 DXGI 线索，不能断定 DX11/DX12。
最新版底层已经返回 `unknown / ERR_API_SELECTION_REQUIRED`；本轮修正仍显示
“暂不支持”的徽标，改为“API 待确认”，并保留卡片中的 API 选择入口。

新增真实 service/library/installer/file-journal 临时目录回归：正确 EXE 手动指定
DX12 后使用明确选择的核心、保留 nrchain、移出并备份旧 carrier；重新扫描及重开服务
保留 EXE 绑定的选择，改选 EXE 不继承。32 位、普通 DLSS 缺失、载荷损坏继续拒绝。
DX11 仍只接受配套兼容组件，不静默覆盖核心版本选择。

此处依据截图字段制作夹具，没有使用该玩家的实际 EXE 或游戏运行日志；不能称作
《看门狗：军团》已实机兼容通过。

## 独立 carrier 勾选

先只开放 DX11 carrier，不泛化为任意 Addon/运行库混装。

- DX12/未确认 API 的 carrier 选项保持未勾选且不能启用；DX11 可单独勾选。
- 取消并应用：游戏关闭检查、原有反作弊确认和文件事务通过后，移出已管理及已知旧
  carrier，保留核心、共用 nrchain 和其他 Addon；持久备份、原版卸载恢复继续有效。
- DX11 取消后不会假装路线完整；安装、修复或 OTA 不会静默重新勾选。重新勾选先保存
  选择，再由用户安装/修复配套组件。仍保留版本匹配和哈希检查。
- 组件偏好独立存为 EXE 绑定的 `carrierEnabled / carrierExecutable`，API 与核心
  选择不受影响；manifest 的 `carrierDisabledByUser` 也保护直接调用修复的路径。
- 文件改动导致关闭被拒绝时，保持游戏文件并还原组件偏好，避免界面谎报已关闭。

接口入口：`setGameCarrier(id, enabled, options)`；实际关闭为 installer 的
`disableCarrier`。未来 Release 只需在既有配套验证之上补齐新组件身份和支持范围，
不应取消哈希配套要求。

## 本地证据

Luna max 协助补齐军团场景的服务级回归。父任务验证独立组件关闭/重新启用、拒绝改写
用户文件、直接修复尊重关闭偏好，以及界面徽标/勾选状态。
完整测试日志：`build/dxgi-initial-tests.log`；界面夹具截图：
`build/dxgi-before-choice.png` 与 `build/dxgi-after-choice.png`。
截图使用隔离用户数据和假游戏数据，执行实际 renderer/preload，不扫描或操作真实游戏。
