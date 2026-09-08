# Manager 原生 NVIDIA 设置实机验收

2026-09-09 02:53（Asia/Shanghai），真实 NVIDIA DRS 写入、读回和恢复验收通过。未运行游戏、复制的 EXE 或任何 GPU 工作负载；此结果不证明游戏已启用 SR 或 4× 补帧。

- 实际检测：NVIDIA GeForce RTX 5090 Laptop GPU，`series: ["RTX50"]`。
- 验收对象：由系统 x64 `where.exe` 复制的唯一 EXE，仅用于独立应用配置身份。写前确认无已有应用 profile；未对真实游戏或全局 profile 写入。
- 调用路径：当前 `createLaunchSettingsService` 与真实 `createNvapiProfileAdapter`，保留实际进程、硬件、身份及恢复保护；依次 `save` → `preview` → `apply({confirm:true})`。
- SR 请求：`{backend:"native", quality:"custom", renderPercent:67, preset:"K"}`。这里验证的是明确的自定义 67% 输入比例；预设 `quality` 使用另一种策略编码，未将两者混称。
- FG 请求：`{backend:"nvidia", mode:"fixed", multiplier:4}`。
- 两域应用后，所有 8 项驱动设置读回符合预览写入值：SR override=1、mode=6、ratio=67、preset=11；FG mode=2、count=3、dynamicMax=0、target=0。FG count=3 表示 4× 总倍率请求中的生成帧数量。
- `finally` 中依次 `restore("fg")`、`restore("sr")`，每步再次读取全部 8 项。最终快照与写前持久化基线深度严格相等：全部设置 `absent`、`profile:null`；`pendingAfter:[]`，无应用或清理错误，进程退出码 0。

验收文件保留于 `build/nvapi-live-168da9ea79db49acb9edd84bdc965977/`：

- `probe.cjs`：本次执行脚本。记录文件采用独占创建，不应重复执行同一目录。
- `hardware.json`、`identity.json`：设备和未执行的临时 EXE 身份。
- `baseline-eight-settings.json`：写入前已持久保存并 fsync 的 8 项与 profile 基线。
- `sr-preview.json`、`fg-preview.json`：真实写入计划。
- `sr-readback-eight-settings.json`、`fg-readback-eight-settings.json`：应用后完整读回。
- `fg-restore.json`、`sr-restore.json` 及对应读回文件：逐域恢复结果。
- `final-eight-settings.json`、`result.json`：最终一致性与无 pending 结论。

本次仅新增验收文件与本文，没有改动生产代码、驱动或安全策略。`runtimeVerified:false`，`workloadStarted:false`。
