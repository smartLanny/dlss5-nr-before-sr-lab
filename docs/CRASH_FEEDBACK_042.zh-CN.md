# beta0.4.2 离线崩溃反馈

普通 `Collect-Feedback.cmd` 保持原来的低敏感范围，不复制转储。发生闪退后需要定位异常码时，双击 `Collect-Crash-Feedback.cmd`。`collect-crash-feedback.ps1` 与原来的 `collect-feedback.ps1` 必须都在旁边。

崩溃模式先读取允许目录中最新的 `ReShade.log`，只接受完整 ReShade 初始化行里的绝对 `.exe` 路径。一个日志中出现两个不同的规范绝对路径时，即使 EXE 文件名相同，也会停止崩溃材料查询。确认唯一目标后，它只处理最近 72 小时内符合下列身份规则的材料：

- Windows Application Error 1000 和 Windows Error Reporting 1001 的字段摘要；记录带 `AppPath` 时必须与当前游戏完整路径一致，没有路径时才按精确 EXE 文件名回退；
- WER `ReportArchive` / `ReportQueue` 中已有的匹配 `.wer`，同样优先执行完整路径匹配；
- 匹配 WER 目录内的已有转储，以及默认 `CrashDumps` 中名为 `目标.exe[.数字].dmp` 的已有转储。

独立 `CrashDumps` 的文件名不包含来源 EXE 完整路径，因此无法区分电脑上另一个目录里的同名程序。工具不会扩大目录搜索，并在 `crash-evidence.json` 中把该项明确标为“仅文件名与时间”；用户发送前仍应检查隐私内容。

目录最多向下一层，所有根、目录条目、候选数、文件数和字节数均有限制。WER 单文件最多 4 MiB；转储单文件最多 192 MiB、最多两份；新增崩溃材料合计最多 256 MiB。超过限制、读取失败和没有找到材料都会写进 `crash-evidence.json`，不会用“完整”掩盖缺失。

工具不创建转储、不启用 LocalDumps、不修改注册表、不启动游戏、不上传。转储可能包含进程内存、用户名和本机路径。ZIP 只保存在本机，发送前由用户决定是否分享。没有事件或转储只表示当前可读位置没有留下证据，不能证明游戏没有崩溃。

回归门禁 `scripts/validate-offline-feedback.py` 包含地平线 6 缺证据样本、1000/1001 路径与文件名分级匹配、同名双路径 fail-closed、相似 EXE 拒绝、时间窗口、WER 一层边界、共享根目录隔离、转储数量和字节上限。默认收集器仍验证不复制 `.dmp`。
