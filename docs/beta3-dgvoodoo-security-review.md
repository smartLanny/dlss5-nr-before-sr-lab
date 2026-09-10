# dgVoodoo 2.87.4 下载告警核查

2026-09-10：此组件暂不允许进入 beta.3 游戏部署或交付包。源一致性已确认，Defender 检测尚未解决，不能将本记录解读为安全背书。

## 当前证据

- 文件：`dgVoodoo2_87_4.zip`，9,235,426 字节。
- SHA-256：`74aeb464d829db80e3f4aa8fae235e6e3b38fc01188776c5c2376bb0dea0956e`。
- [作者官方发布](https://github.com/dege-diosg/dgVoodoo2/releases/tag/v2.87.4)中的同名资产摘要与本地一致。
- Defender 在 2026-09-10 17:39:49（UTC+8）以实时保护检出 `Trojan:Win32/Kepavll!rfn`。记录显示 `DidThreatExecute=false`。仅下载和读取压缩包目录，尚未解压、执行或部署其中任何程序、DLL。
- 本机规则从 `1.459.132.0` 更新至 `1.459.142.0` 后，对完整 ZIP 的定向扫描仍检出同一威胁；引擎 `1.1.26080.3`，退出码 `2`。
- 扫描使用微软文档规定的仅检查模式：扫描压缩包，忽略文件排除，仅输出检测结果。实时保护保持开启，没有添加排除或允许规则。
- 作者发布说明称微软已复核并移除检测；此声明与本机当前结果不一致。需要供应商再次确认具体摘要及检测规则。

## 交付处理

这份下载不进入资源目录、游戏或成品包。原始字节及日志保留用于复核。不会通过改名、加密压缩、改写二进制或关闭防护来改变检测结果。

[作者当前分发条款](https://dege.freeweb.hu/dgVoodoo2/ReadmeGeneral/)另限制将 dgVoodoo 捆绑在用于多个应用的通用启动器或框架中。通用 Manager 配套需采用允许分发的替代实现；原工具的逐游戏外部配套接口与成品捆绑分开处理。

最终成品必须重新扫描原始文件及交付压缩包，记录当次摘要、Defender 引擎、规则和输出；任何扫描失败或检测命中都会阻断该候选交付。这只证明当次检测结果，不能保证其他安全软件或未来规则不再提示。

## 微软复核材料（尚未提交）

提交类别：Software developer / suspected incorrect detection。产品：Microsoft Defender Antivirus。检测名称、文件名、摘要、引擎、规则、来源及复现步骤如上。待复核的问题：作者已声称复核完成的确切发布资产，仍被更新后的本机规则识别。

[微软文件提交入口](https://www.microsoft.com/en-us/wdsi/filesubmission)；[软件开发者常见问题](https://learn.microsoft.com/en-us/defender-xdr/developer-faq)。微软不提供“永久免误报”保证；可信代码签名用于验证发布者，不能代替恶意软件检测。
