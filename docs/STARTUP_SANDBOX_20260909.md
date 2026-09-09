# 管理器子进程启动失败：临时兼容入口

针对当前 dev.13 管理器的最小修正候选，不修改核心、游戏文件或默认沙箱策略。

## 已知证据

用户转述目标电脑为 Windows 11 26200、RTX 5070 Ti，有 GameViewer 虚拟显示适配器。普通启动有 `GPU process launch failed: error_code=18`、`render-process-gone: launch-failed / exitCode=18`、`ERR_FAILED`，随后 Chromium FATAL 和 `0x80000003` 退出。

目标机测试 `--disable-gpu` 无效，`--disable-gpu-sandbox` 未完成界面就绪；`--no-sandbox` 在便携版与已安装主程序上均完成 `window-visible → page-loaded → renderer-ready`。

这支持“沙箱相关子进程启动失败”的定位，不证明 GameViewer、GPU 硬件或具体 Windows 策略就是根因。NSIS Setup 的 `System.dll / 0xc0000005` 是另一条安装器故障，不能宣称此兼容入口修复了 Setup。

## 现有版本的使用

将独立兼容工具解压。把**便携版 EXE**或已安装的**DLSS 5 AI 超分管理器.exe**拖到 `兼容启动.cmd`，阅读提示后输入 Y。也可把工具放在仅有一份管理器 EXE 的目录里，双击入口。

此操作仅向选定程序的本次启动传入 `--no-sandbox`，不会修改全局配置、游戏、原快捷方式或原 EXE，不会自动结束旧进程。直接运行原 EXE 仍走正常路径。工具拒绝 Setup、非管理器文件和无法唯一选择的目录。

`已发起兼容启动` 只表示系统接受启动请求。验收以当前会话真实窗口出现、页面完成加载及 renderer-ready 为准；进程存活或文件检查通过不能替代这些信号。

这是临时排障方式，会关闭该管理器所有 Chromium 子进程的沙箱，并非仅关闭 GPU 沙箱。Electron 官方建议保留沙箱，不能把它包装成面向所有用户的默认修复。[Electron 沙箱文档](https://www.electronjs.org/docs/latest/tutorial/sandbox)

## 源码修正

- `launch-failed` / `integrity-failure` 使用独立子进程启动失败分类，不再引导无效的软件渲染重试。
- 日志保留 type、reason、exitCode、Chromium/系统版本和有效启动选项。
- 正常渲染崩溃仍保留原有软件渲染排障入口。
- 默认 `sandbox:true`、`contextIsolation:true`、`nodeIntegration:false` 保持不变，不自动降级或永久写入兼容开关。

## 默认启动的下一步验证

需要目标机继续验证，本文没有接入那台电脑。优先比较同一应用/运行时的管理员启动与非管理员只读启动，两者保持沙箱开启；只有 manifest 改为 asInvoker 不够，因为当前主程序仍会自动提权。若权限不是区分因素，再做同权限、同应用代码下的 Electron 运行时 A/B。

不自动重置目录 ACL，不停用 GameViewer，不修改安全软件或系统策略，不同时改变多个环境变量。另行捕获 NSIS Setup 的安装阶段日志，再确定它的最小修正。
