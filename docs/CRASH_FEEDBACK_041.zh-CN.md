# beta0.4.1 离线崩溃反馈

普通 `Collect-Feedback.cmd` 保持原来的低敏感范围，不复制转储。发生闪退后需要定位异常码时，双击 `Collect-Crash-Feedback.cmd`。`collect-crash-feedback.ps1` 与原来的 `collect-feedback.ps1` 必须都在旁边。

崩溃模式先读取允许目录中最新的 `ReShade.log`，只接受完整 ReShade 初始化行里的绝对 `.exe` 路径。一个日志中出现不同 EXE 名称时会停止崩溃材料查询。确认唯一名称后，它只处理最近 72 小时内精确匹配该文件名的：

- Windows Application Error 1000 和 Windows Error Reporting 1001 的字段摘要；
- WER `ReportArchive` / `ReportQueue` 中已有的匹配 `.wer`；
- 匹配 WER 目录内的已有转储，以及默认 `CrashDumps` 中名为 `目标.exe[.数字].dmp` 的已有转储。

目录最多向下一层，所有根、目录条目、候选数、文件数和字节数均有限制。WER 单文件最多 4 MiB；转储单文件最多 192 MiB、最多两份；新增崩溃材料合计最多 256 MiB。超过限制、读取失败和没有找到材料都会写进 `crash-evidence.json`，不会用“完整”掩盖缺失。

工具不创建转储、不启用 LocalDumps、不修改注册表、不启动游戏、不上传。转储可能包含进程内存、用户名和本机路径。ZIP 只保存在本机，发送前由用户决定是否分享。没有事件或转储只表示当前可读位置没有留下证据，不能证明游戏没有崩溃。

回归门禁 `scripts/validate-offline-feedback.py` 包含地平线 6 缺证据样本、1000/1001 精确匹配、相似 EXE 拒绝、时间窗口、WER 一层边界、目标含糊 fail-closed、转储数量和字节上限。默认收集器仍验证不复制 `.dmp`。
