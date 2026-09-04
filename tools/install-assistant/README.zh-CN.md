# DLSS5 安装助手

这是一个 Windows WPF 自包含安装助手。它只导入用户本地安装包，逐文件校验 SHA-256，安装前建立事务备份；卸载时如果文件已被后来修改，会保留该文件。扫描与手选不提权，启动按钮使用 Steam URI 或原始 EXE。

## 安装包清单

安装包目录必须包含 `manifest.json` 与清单列出的文件：

```json
{"id":"nr-before-sr","build":"beta0.3.3.6","nativeD3D12":true,"onlineAllowed":false,"files":[{"path":"nrchain_nvngx.dll","sha256":"<64位十六进制SHA-256>","required":true}]}
```

`path` 只能是包根下的相对路径。清单不包含签名信任，因此只应导入可信来源；助手不会从网络下载游戏 DLL。

## 运行时状态

游戏目录可写入 `runtime-status.json`：`{"schema":"dlss5-runtime-status/v1","state":"active","buildId":"...","active":true,"bypassReason":"","updatedAt":"2026-09-04T00:00:00Z"}`。助手把部署完成和运行时 `active` 分开显示；没有该文件时不能声称已生效。PID、启动时间等本次会话证据由主程序后续扩展，旧日志不作为本次运行证明。

Steam 默认只列出能从 EXE 判断为 D3D12 且同目录发现 `nvngx_dlss.dll` 的候选；手选 EXE 仍允许用户查看。Helldivers 2 永远不自动部署，以避免触碰反作弊边界。已有未知 `dxgi.dll` 时拒绝覆盖；ReShade DLL 只有检测到 ReShade 标记才允许继续。
