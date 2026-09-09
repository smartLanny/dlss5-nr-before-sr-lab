# 管理器启动与恢复

默认双击管理器使用普通权限，并保留 Chromium 沙箱。浏览游戏列表、查看设置不再先触发管理员授权。安装、修复、恢复和启动游戏继续使用原有的游戏关闭检查、目录写入检查及事务记录。

“设置 → 启动与权限”显示本次启动模式与当前权限。确有权限不足时，错误会给出“以管理员身份重启”的入口，不会自动重复失败的修改。点击该按钮即表示请求管理员重启，随后由 Windows UAC 授权；原窗口保持打开并暂停修改，直到新管理员会话完成窗口显示、页面载入和界面初始化。新进程仅存活、旧会话的回执或部分就绪都不算成功。

取消 UAC、子进程失败或超时会保留原窗口。若新实例尚未释放启动锁，旧窗口继续暂停修改；关闭新实例后可在设置中刷新状态。恢复记录仍按既有事务规则处理。

## 普通启动失败时

1. 运行独立的“启动诊断.cmd”，它不依赖 Electron 能否启动。
2. 需要临时排查时，把便携版或已安装的管理器主程序拖到“兼容启动.cmd”。不能选择 Setup 安装器或游戏 EXE。
3. 阅读提示并输入 Y，才会仅对本次启动使用 `--no-sandbox`。此模式降低 Chromium 子进程隔离保护；不更改系统策略、快捷方式或默认启动配置。
4. 设置中会明确显示“临时兼容启动”。下次直接打开原 EXE 即恢复正常沙箱模式。

独立兼容工具记录本次确认、取消、启动请求或错误，路径由工具显示，并可被独立启动诊断收集。发起进程不等于界面启动成功。完整解压 ZIP 解压后直接运行管理器主程序，跳过 NSIS 安装器。NSIS Setup 的 `System.dll` 崩溃属于安装器问题，不因管理器的兼容模式能打开而自动判定已修复；当前尚未证明目标电脑上的 Setup 崩溃已修复。

## 短启动验收

相关单元、握手与普通权限游戏启动助手测试：

```powershell
node --test --test-concurrency=1 test/main-startup.test.js test/startup-diagnostics.test.js test/startup-elevation.test.js test/startup-handoff.test.js test/startup-compatible.test.js test/startup-ui.test.js test/game-launch-broker.test.js
```

真实 Electron 短启动检查：

```powershell
node scripts/startup-runtime-smoke.js --electron "C:\build-tools\electron\electron.exe" --output startup-runtime-result.json
```

检查打包后的实际 ASAR 界面时可附加：

```powershell
--main "C:\candidate\resources\app.asar\main.js"
```

检查使用与候选版一致的 Electron 运行时，加载生产 `main.js`、preload 和 renderer。游戏服务使用不执行修改的隔离数据；每种模式都有独立临时 userData。普通模式和显式兼容模式分别通过真实 Windows 启动助手获得非管理员令牌，并要求真实窗口可见、`page-loaded`、`renderer-ready`、`window-visible` 三项齐全，preload 连接正常且配置仍为 `sandbox: true`、`contextIsolation: true`、`nodeIntegration: false`。兼容模式的全局临时开关单独记录。

该检查不操作游戏、注册表或驱动设置，不代替安装器验收、实际发行 EXE 的 manifest 核对、目标电脑复测或长时间图形稳定性测试。现场 Win11 26200 的已知现象仍是“沙箱子进程启动失败、临时关闭沙箱后可打开”；具体系统诱因及 GameViewer 是否参与，尚未确证。

完整解压版的只读静态检查：

```powershell
node scripts/verify-manager-release.js --dir "C:\candidate\win-unpacked" --electron-dist "C:\build-tools\electron" --nsis-dir "C:\build-tools\nsis" --output "C:\candidate\manager-file-verification.json"
```

检查对照当前受信构建源码的打包清单，校验 ASAR 内源码、全部配套资源和启动辅助工具；Electron 运行文件及语言资源与构建运行时逐文件核对 SHA256，并解析管理器实际 EXE 的 Windows 应用清单，要求 `asInvoker`。若构建器加入了 `resources/elevate.exe`，须用 `--nsis-dir` 指定本次受信 NSIS 构建目录核对该文件；未列入清单的额外文件会令检查失败。JSON 报告必须放在候选目录之外。此检查不运行候选程序，不能据此声称界面启动或 NSIS 安装成功。

正式 EXE 的隔离短启动使用另一入口：

```powershell
node scripts/startup-runtime-smoke.js --executable "C:\candidate\win-unpacked\DLSS 5 AI 超分管理器.exe" --output "C:\candidate\manager-runtime-verification.json" --logs "C:\candidate\runtime-logs"
```

此入口通过 [Electron 官方支持的主进程调试开关](https://www.electronjs.org/docs/latest/tutorial/debugging-main-process)，让实际 EXE 在打包 `main.js` 第一行执行前暂停。测试先核对 PID 与主入口身份，再安装临时 userData、独立日志与无动作的游戏服务，随后继续原始 ASAR 的生产主进程、preload 和界面。EXE 和 ASAR 的 SHA256 在运行前后核对；报告记录普通令牌、窗口可见和三项就绪事件。此测试保留本机调试仪器的影响，只证明这份正式 EXE 在当前电脑的隔离短启动结果，不能代替无仪器双击、NSIS 安装或目标电脑的复测。使用 `--electron` 的源码短启动也会在报告中明确区分。

上一项还会在安装测试隔离层之前，只读确认 `--user-data-dir` 已令 `app.getPath('userData')` 指向新临时目录。确认通过后，可进行无调试仪器的正式应用短启动：

```powershell
node scripts/startup-uninstrumented-smoke.js --executable "C:\candidate\win-unpacked\DLSS 5 AI 超分管理器.exe" --isolation-proof "C:\candidate\manager-runtime-verification.json" --output "C:\candidate\manager-uninstrumented-verification.json" --logs "C:\candidate\uninstrumented-logs"
```

检查强制要求上一项证明与当前 EXE、ASAR 的 SHA256 一致。实际启动只传临时配置目录参数，不带调试开关、不替换游戏服务、保持默认沙箱；允许程序正常只读发现实际游戏，不点击安装或应用设置。观察约 15–20 秒，用生产启动日志的三项就绪事件及系统窗口可见性判断，再只关闭已核对路径、创建时间和 PID 的本次窗口。这能验证当前电脑上新配置下的普通启动，仍不代表目标电脑或原有配置环境已通过。
