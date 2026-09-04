# feedback archive 2 只读排查

归档：`C:/Users/pc1/Downloads/归档 2.zip`。中央目录共 14 个条目：7 个中文日志与 7 个 `__MACOSX/._` 元数据副本；文件名按 ordinal 分开处理。已安全解压到 `test-output/feedback-archive2`，检查了相对路径与总展开大小（约 0.7 MB），未读取任何二进制。

## 可复现的关键证据

1. **beta0.4-perfect-chain-r4 的实际失败点是 native pipeline 未绑定。** `最后生还者 2nrbeforesr.log:1` 标识 `build_id=beta0.4-perfect-chain-r4`；`:43` 首次 contract 已是 `Feature18-success=0`；`:68` 起反复 `native-hooks-unavailable`，`:369` 起每 600 次请求均为 `Feature18-success=0 transfer-submitted=0`，同时 `pipeline-unbound`。当前 `src/nr_command_state.inl:1048-1052` 只有在 `meta_covered && state != nullptr && hooks_ready && CanRestoreNativeCommandState(...)` 全部满足时才捕获；`hooks_ready=false` 直接给 `native-hooks-unavailable`，已知状态但没有记录 PSO 时给 `pipeline-unbound`。`src/nr_command_state.inl:981-989` 会在 BeforeReset 重试安装 native hooks，但此分支与 `973d22d` 相比无代码差异（`git diff 973d22d..HEAD -- src/nr_command_state.inl` 为空），所以本归档没有证明该问题已被维护修复。最小修复应先确认同一 native list 的 Reset/SetPipelineState hooks 真实发布并记录首个 PSO，再允许 Feature 18；回归必须覆盖 hook 发布失败、Reset 后 PSO 记录和 pipeline 缺失三态。安装器/状态层仍应把“插件加载”和“NR 生效”严格分开，不能以 addon/DLL 存在判成功。

2. **beta0.3.3.5 的相同症状已有恢复与抖动证据。** `最后生还者 2.log:1` 为 `beta0.3.3.5`，`:119` 首次 `native-hooks-unavailable`，`:399-432` 在 `none/active` 与 `command-state-unverified` 间多次切换；`:428-432` 的相邻状态行精确显示该抖动；后续 `:457`、`:484`、`:514`、`:541`、`:570`、`:598`、`:627`、`:676`、`:704`、`:733`、`:761`、`:791` stats 显示 transfer-submitted 增长。维护源码已有 `src/nr_command_state.inl:1045-1068` 的按 carrier 重试路径，但没有证据证明该归档中所有窗口都稳定满足条件。修复方向是连续窗口与 transfer 增长门槛，而不是一次成功状态。

3. **0.3.2 正常样本说明对照基线。** `0.3.2 正常，0.3.3.5 无法打开游戏逆战未来.log:1` 为 `beta0.3.2-dev-r2`，`:8` `MH_EnableHook all status=0`；`:10` observer active；`:405` session totals 已有 `Feature18-success=598`、`transfer-submitted=595`、`lifecycle-bypass=0`，`:433` 增至 1198/1195。这支持将兼容表按 build/游戏/API/稳定 transfer 证据维护。

4. **鬼武者“无法打开”不能归因于退出警告。** `0.3.3.5 鬼武者无法打开.log:1` 是 `build_id=beta0.3.3-dev-r1`，`:12` `MH_EnableHook all status=0`；ReShade 对照 `鬼武者 reshade.log:173` 仅报告 `Add-ons are still loaded! Application may crash on exit.`。仓库 `docs/KNOWN_LIMITATIONS.md:185-186` 明确永久 detour 会 pin 到进程退出，这一行可能是永久 PIN 设计的正常警告，不能据此优先拆卸 hook。当前可行动项是收集真实崩溃码/dump，并检查启动阶段日志；不能把 status=0 当作游戏可用证明。

5. **外部 ReShade 配置存在真实搜索路径缺失。** `最后生还者2reshade.log:305-306` 报 `gravity_noise.png` 不在任何 texture search path；对应实现应读取并显示现有 `ReShade.ini` 搜索目录，缺失资源时提示用户修复路径，不能静默复制 DLL 或覆盖未知 `dxgi.dll`。

6. **第三方 MFG Unlock 线索不能当 DLSS5 证据。** `最后生还者ReShade.log:146,150,152,154` 多次报告对两个 `nvngx_dlssg.dll` 未找到 temporal-kernel descriptor；该日志还在 `:223` 报 add-on 未卸载。安装器应将这类外部 add-on/patch 记录为冲突或未知来源，禁止纳入本项目兼容表。

## 最小可修复项

- 在 `runtime-status` 中只在当前进程、当前 build、连续采样窗口内 `transferSubmitted` 严格增长且无 bypass 时置 `active=true`；`pipeline-unbound`、`native-hooks-unavailable` 立即降级为待确认。
- 对 ReShade 解析 `AddOnPath`/texture search paths，安装包清单必须声明 `role/source/target`；路径缺失或同名未知 DLL 时拒绝写入并给出实际路径。
- 将 addon unregister 和退出清理做成幂等流程，增加“仍有 add-on loaded”诊断字段；不要以 `MH_EnableHook status=0` 单点结论替代完整运行时证据。
- 把 `beta0.4-perfect-chain-r4` 与本次《最后生还者2》样本标为“加载但未生效”，不要声称游戏验证通过。
