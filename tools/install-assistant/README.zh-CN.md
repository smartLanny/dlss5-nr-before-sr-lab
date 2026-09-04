# DLSS5 安装助手

这是一个 Windows WPF 自包含安装助手。它只导入用户本地安装包，逐文件校验 SHA-256，安装前建立事务备份；卸载时如果文件已被后来修改，会保留该文件。扫描与手选不提权，启动按钮使用 Steam URI 或原始 EXE。

## 安装包清单

安装包目录必须包含 `manifest.json` 与清单列出的文件：

```json
{
  "id":"nr-before-sr", "build":"beta0.3.3.6", "nativeD3D12":true, "onlineAllowed":false,
  "verifiedNativeD3D12":{"verified":true,"method":"validated-native-d3d12-evidence","api":"D3D12","executableSha256":"<EXE-SHA256>"},
  "compatibleProfiles":[
    {"steamAppIds":["1091500"],"executableSha256":[],"verifiedNativeD3D12":true},
    {"steamAppIds":["292030"],"executableSha256":[],"verifiedNativeD3D12":true}
  ],
  "files":[
    {"path":"nrchain_nvngx.dll","sha256":"<64位十六进制SHA-256>","role":"addon","source":"trusted-local","target":"reshade-addon","required":true,"allowedExistingSha256":["<已验证ReShade loader SHA256>"]}
  ]
}
```

`path` 只能是包根下的相对路径。`compatibleProfiles` 必须绑定具体 Steam AppId 或 EXE SHA256，不能使用 `native-d3d12` 这类泛标签；当前首批自动兼容证据只覆盖 Cyberpunk 2077（1091500）和 The Witcher 3（292030）。Helldivers 2（553850）永久禁止自动部署。`allowedExistingSha256` 只允许包发布者列出的已验证 ReShade loader 哈希，未知同名 DLL 或 addon 一律拒绝覆盖。清单不包含签名信任，因此只应导入可信来源；助手不会从网络下载游戏 DLL。

## 运行时状态

主程序应在 `%LOCALAPPDATA%/DLSS5InstallAssistant/runtime/<PID>.json` 写入 `{"schema":"dlss5-runtime-status/v1","state":"active","buildId":"...","active":true,"processId":123,"processStartedAt":"2026-09-04T00:00:00.000Z","updatedAt":"2026-09-04T00:00:00.000Z","executable":"C:/Game/game.exe","nativeSrObserved":true,"nrEvaluations":1,"transferSubmitted":1,"configGeneration":1}`。助手会校验当前 PID、进程创建时间、EXE 全路径和 15 秒新鲜度；没有通过不能声称已生效。

Steam 默认只列出能从 EXE 判断为 D3D12 且同目录发现 `nvngx_dlss.dll` 的候选；手选 EXE 仍允许用户查看。Helldivers 2 永远不自动部署，以避免触碰反作弊边界。已有未知 `dxgi.dll` 时拒绝覆盖；ReShade DLL 只有检测到 ReShade 标记才允许继续。
