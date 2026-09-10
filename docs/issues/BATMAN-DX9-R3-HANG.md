# Batman: Arkham Asylum GOTY — DX9 R3 实机卡死

状态：**实机验收失败，根因未确定；作为独立问题保留，不阻塞 HoYo 流程。**

```json
{
  "userReportedFailure": true,
  "realGameAcceptance": "failed",
  "rootCauseConfirmed": false,
  "blocksHoYoWorkflow": false
}
```

用户报告最终 R3 在真实 Batman 中约 30 FPS，随后卡死，且未看出 NR 效果。这是本次验收结果；此前小程序通过不能覆盖或改写它。未录制独立 FPS 基线或画面对照，因此“约 30 FPS”和视觉感受保留为用户报告。

## 本轮身份

游戏进程 `100340`，启动时间 `2026-09-10 22:22:38.415 +08:00`；NR 宿主 `32324`。实际为 x86 DX9，3840×2160 全屏、无 MSAA，NVIDIA GeForce RTX 5090 Laptop GPU，日志驱动版本 `610.62`。

磁盘身份采集于 `2026-09-10 23:06:41 +08:00`；宿主在采集前已经退出。下列组件与冻结 R3 清单一致，不能据此把稳定性或画质标记为通过。

| 文件／角色 | SHA-256 |
| --- | --- |
| `ShippingPC-BmGame.exe` | `4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028` |
| DX9 provider R3 | `250e4a5523a20c54a0c59dc55f08f81cc38526efa1eb6c920a86e3602fd499f8` |
| Core `0.4.7beta` ExternalProvider | `93011d9283615ea9dc8e92955f5ca6aeff01435925f63e941dc1eea1128a372c` |
| x64 NR 宿主 | `60c7b068f29868c8b1bdb575960dc89d2259860b7bf77e51c52e44870df0034c` |
| 系统 D3D9On12 入口 shim | `74a9a415cf8883bed5e56eaacd2a1c3b3da2a3f68c89a0db249c6a69b16bee85` |
| ReShade x86 | `da430e0a9c6eecefa0d1b27d05e16c426fb5d04e808b194d914eaac4b31bc0f8` |
| RTX50 NR 运行库 | `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e` |

完整路径、配置、文件时间、全部摘要和日志快照见 [triage.json](../../build/batman-dx9-r3-hang-20260910-222238/triage.json)。冻结配套指纹为 `34820cd0c2f98cf830d85a09e70378bfda30a68a033128d522c0056d0528d80d`。

## 两项有界鉴别

1. **当前会话日志与存活进程。** 同一游戏／宿主回执中有 38 条采样记录明确标记 `output_ready=1 nr_completed=1`，从 `22:23:22.861` 的 frame 90 到 `22:24:17.693` 的 frame 1200。深度日志为 `current-frame-scene`、3840×2160；本轮已经跨过旧版“Present 时深度解绑导致完全不提交”的故障点。连续区间 frame 360→1200 的宿主计数推进约为 28.8 帧／秒，与用户所述量级相近，但不是独立游戏 FPS，也没有无 NR 基线来归因性能损失。

   最后回执之后没有新的 provider 处理记录。`22:30:41.916` 宿主记录 `pipe closed by the game`，随后 `pending game fence waits released; exiting` 和 `exit 0`。游戏侧 ReShade 在 `22:30:41.895` 销毁 runtime，`22:30:42.065` 卸载 provider 并报告 `Reference count for IDirect3DDevice9 ... is inconsistent (238)`。引用计数告警值得保留，但这段日志不能证明责任模块或它与先前卡死的因果关系。

   只读窗口枚举发现仍存活的游戏进程在自己的 `EXIT TO DESKTOP` 确认框，正文提示未保存进度将丢失；主线程 `19580` 等待 `UserRequest`，Windows 标记 Responding=true。没有点击按钮或结束进程。这个退出阶段快照不能代表卡死发生时的线程状态，也不能否定用户此前经历的卡死。

2. **已有系统异常记录。** 查询从 `22:20 +08:00` 起的 Application 1000／1001／1002、System 4101，并检查当前用户 CrashDumps 和 WER ReportArchive／ReportQueue 中匹配 Batman／provider 名称的现有记录，未找到匹配事件或转储，事件查询无错误。当前会话没有 `GPU completion unconfirmed`、`resource return failed`、`shared set rejected`、timeout 或 device removed 的明确 provider 记录。[查询范围与结果](../../build/batman-dx9-r3-hang-20260910-222238/windows-evidence.json)仍保留；“未找到”不等于排除 GPU 或同步问题。

## 关于专用 DX9 实现

DX9 的确走独立实现：项目 shim 调用 Windows 系统 D3D9On12，provider 在 Present 中借出颜色／深度资源，经本帧 GPU 完成等待和 x64 宿主处理后写回，再归还 D3D9 资源。相关边界见 [DX9 provider](../../scripts/feeder-beta3/nr_feeder_d3d9.cpp) 的 `Complete`、`Present`、`host.Submit` 和 `ReturnUnderlyingResource`。它增加了 DX9 互操作及同步边界，与其他 API 的接入方式不同。

这些结构差异是事实，也给后续定位提供检查位置；当前没有卡死瞬间的调用栈、失败 HRESULT 或对照数据，无法据此断言“专用 DX9 实现就是根因”，也无法把原因归给游戏、Core 或驱动。已有 NR 完成回执只证明曾完成计算与回执，不证明用户看到了有效提升。

## 后续范围

本轮不提出未经证据支持的小补丁，不更改真实游戏、R3 分发或原受控验收记录，也不追加 GPU 长测／替代图形接口实验。若后续继续此问题，最高价值的一项证据是：**在卡死现场、进入退出确认框之前，捕获同一游戏和宿主的线程等待／调用栈**，区分 Present／资源归还／宿主等待与游戏自身阻塞。获得该证据后再决定是否能做小修复。

本轮动作：仅只读排查和保存本地 issue；启动游戏 0 次、结束进程 0 次、修改游戏文件 0 项、修改分发文件 0 项。
