# Manager line C：hotfix.6 基线上的启动失败与外部修改修复

## 主执行线程复核结果（以本节为准）

执行依据仍为固定 `0838ffb549a069dbcef9ac02914907be851d805d` 计划；从 #181
`94489177be989ab316fc9167ade41f9d75ce1a10` 新建隔离 worktree，分支为
`codex/manager-recovery-20260908-cloud`。旧 product、旧 Manager 检出和已安装程序未改动。

候选 Manager 使用独立版本 **0.4.6-hotfix.7-dev.1**（PE buildVersion `0.4.6.7`）；
默认内置 Core 仍为 **0.4.6-hotfix.1 · Beta**。这不是维护者验收，也未合入 main。

主线程在初始补丁之外复现并修复：

1. **no-op 预览失效**：原实现对“无需修改”的计划直接跳过。预览后驱动键、profile 身份或
   RTX40 控制文件被外部修改，仍会报告跳过成功。新增驱动/profile 与文件反例先在原实现失败，
   然后在生产 `apply` 的 no-op 路径加入最新快照/hash 校验；冲突明确拒绝，外部数据、receipt
   和 pending 状态保持原样。无已管理域的恢复仍是无需探测后端的真正 no-op。
2. **抛异常的旧 SR 路径**：不仅 `{apply:{ok:false}}`，`hasSrRequest` 或旧 SR helper 直接
   抛错也保留之前已完成的设置结果和原始 cause。读 receipt 失败不能掩盖原始失败。
3. **撤销后的错误恢复提示**：成功撤销也会返回 `applied:true`，不能因此捏造仍存在的恢复记录。
   以最新 receipt 的实际 owned 域为准；无法读取时标记 `recoveryStateKnown:false`，明确要求核对
   并提交 BUG 反馈，而非宣称恢复状态已经验证。
4. **Windows 测试入口**：真实 payload 哈希拒绝测试支持 Windows 自带 PowerShell；缺少独立
   `pwsh.exe` 不再变成无信息失败。仍要求真实启动子进程、匹配原错误、确认历史槽位没有改动。

### 本轮实际验证

- Node 24.5.0 / Windows：完整单测 **258 项，252 pass，0 fail，6 skip**。
- UI 合同：63 HTML IDs / 59 renderer references；4 个生产 JS 文件语法检查通过。
- 真实 Electron 隔离窗口：交互和 900px 布局合同通过；新增失败启动事件后仍显示“游戏未验证”。
  IPC/硬件数据是测试桩，硬件加速关闭；没有真实游戏或驱动写入。
- vendor 固定 `ccb67f4bc92cb5da1d25416dd6b338e12fa432c5`，独立克隆到新 worktree；未改旧 vendor。
- `verify-vendor`、`verify-payload`、`verify-fg-components` 均通过。29 个载荷/FG 二进制按现有
  已校验版本复制到新 worktree，源目录不改；文本 manifest 经 Git 换行过滤仍必须等于固定 blob，
  二进制始终比较字节级 SHA256。
- `git diff --check` 通过。源码和编译/打包、驱动实际效果、游戏画面验证分开记录。

完整测试命令（原始 TAP 和截图只保留本地 `output/cloud-c-20260908-assets-r2/`）：

```text
node --test --test-concurrency=1 --test-reporter=tap \
  --test-reporter-destination=output/cloud-c-20260908-assets-r2/unit-final.tap \
  --test-reporter=spec --test-reporter-destination=stdout test/*.test.js
node scripts/check-ui.js
node scripts/verify-vendor.js
node scripts/verify-payload.js
node scripts/verify-fg-components.js
node_modules/electron/dist/electron.exe test/launch-settings-frontend.electron.cjs \
  output/cloud-c-20260908-assets-r2/manager-ui-900.png
```

6 项跳过是未配置外部历史验收包路径的集成项：PR160 工具 receipt、历史 0.4.5/0.4.6/hotfix.1
OTA 读取，以及历史 0.4.6/hotfix.1 整组部署。没有把这些 skip 计入 pass，也没有以模拟结果冒充
40/50 系真实游戏验收。旧助手的子进程/依赖阻断见下方历史记录，已由主线程在 ROG 正常工具环境
补齐依赖后解决，**不再是当前测试阻断**。

### 当前待验

真实 RTX40 与 RTX50 游戏下的设置采用、生成帧实际输出、异常启动后恢复、外部修改冲突反馈，
仍需维护者验收。不得根据 UI、API 读回或安装器临时目录测试声称游戏运行时通过。此候选未改变
NR 渲染算法、carrier 生命周期、FG 后端或第二套写入协议。发行/ASAR 验证须以另附实际构建记录为准。

---

## 实现助手初始交接（保留历史，不代表上方主线程复核后的状态）

## 范围与基线

- 隔离工作树：`dlss5-nr-before-sr-lab-product-d47dbce3`
- 基线/当前 HEAD：`94489177be989ab316fc9167ade41f9d75ce1a10`（PR #181 latest）
- 授权计划：`0838ffb549a069dbcef9ac02914907be851d805d` 中的
  `docs/plans/2026-09-08-dlss5-execution-plan.md`
- Manager 保持 `0.4.6-hotfix.6`，内置 Core 保持 `0.4.6-hotfix.1 · Beta`。
- 未新增 writer、journal、后端或 Opti 默认路线；设置写入仍只经过
  `launch-settings-service` 和现有 `file-journal`/驱动 pending receipt。
- 未执行游戏、GPU、驱动 profile 或游戏目录写入。

本工作树初始状态干净、处于 detached HEAD。尝试创建唯一分支
`codex/manager-line-c-hotfix6-20260908` 时，共享主仓库 `.git/refs` 不在本工作树可写范围，
Git 返回 `Permission denied`；为保留隔离边界，后续仍在 detached HEAD 上工作，未提交。

## 实际缺陷

`launch-coordinator` 会依次应用启动设置、旧 SR writer（仅未被新 writer 接管时）并创建游戏进程。
当 SR 已成功写入/读回、随后 FG 应用失败，或旧 SR/进程创建失败时，恢复 receipt 仍然存在，
但协调器抛出的错误丢失完整设置结果。`main.js` 因此不会发送
`launch-settings-applied`，展开的 SR/FG 面板不会刷新，用户只能看到启动失败，无法确认哪个域已经
写入并可从现有恢复入口撤销。第二次启动的设置通常是 `noOp/skipped`，仅看本次写入还会漏掉
此前已经 owned 的恢复记录。

## 生产修改

- 启动设置失败、旧 SR 失败和进程 `spawn` 失败统一附带：失败阶段、`gameStarted: false`、
  完整 `launchSettings` 结果和 `recoverableDomains`。
- 失败后只读调用现有 `settings.inspect()`，将 receipt 中已经 owned 的域与本次成功写入域合并；
  读状态失败不会遮蔽原始启动错误。
- 未知进程创建错误归一为 `SETTINGS_LAUNCH_FAILED`；已有 `SETTINGS_`/`NVAPI_`/`FG_` 错误码保留。
- 若失败前已有 applied/skipped 结果，主进程仍通知 renderer 刷新设置面板，但不发送失败行造成重复提示。
- 启动按钮把 game id 交给现有错误 UI，使失败能关联到对应游戏的本地反馈日志入口。
- 不自动回滚已保存的持久启动设置；现有逐域撤销、readback 和 receipt 仍是唯一恢复机制。

## 验证命令与结果

通过：

```text
node test/launch-coordinator.test.js
  11/11 pass
  覆盖部分设置成功后下一域失败、旧 SR 失败、spawn EACCES、既有 no-op receipt、
  公共错误归一化及原有串行/卸载/组件边界。

node test/launch-settings-frontend.test.js
  9/9 pass
  包含失败 IPC 刷新与反馈 game id 合同。

node test/build-config.test.js
  3/3 pass

node scripts/check-ui.js
  UI contract OK: 63 HTML ids, 59 renderer references.

node --check src/product/launch-coordinator.js
node --check main.js
node --check src/renderer/renderer.js
  全部通过。

git diff --check
  通过（仅 Git 提示工作树 LF 将按配置转为 CRLF）。
```

受环境限制未完成：

```text
node --test --test-concurrency=1 ...
  Node test runner 创建测试子进程时返回 spawn EPERM；不是断言失败。

node test/launch-settings-service.test.js
node test/fg-components-settings.test.js
  本隔离检出的 vendor/DLSS5-Swapper 子模块为空，缺少
  vendor/DLSS5-Swapper/src/core/file-journal.js；模块加载阶段停止。
```

没有为通过测试伪造 vendor 或复制第二套 journal。父任务若在完整子模块工作树复核，可运行
`npm test`，以及上述两个真实 service/组件测试。

## 仍需实际游戏验证

- 在一台 RTX 50 机器上保存并应用原生 FG，请人为制造一次可恢复的启动失败（例如选择不会启动的
  测试副本 EXE；不要改真实驱动全局 profile），确认错误显示“FG 设置已写入并保留恢复记录”、
  面板刷新为 readback 状态，并可用“撤销本页设置”恢复。
- 在 RTX 40 且已完整准备兼容组件的隔离游戏副本上，验证同一失败/刷新/撤销路径。
- 验证一次 SR 成功而 FG 因组件或外部配置变化被拒绝的部分成功场景：游戏不得启动，SR receipt
  保留、面板刷新、FG 原因保持原错误码，随后 SR 可逐键恢复。
- 以上只验证 Manager 的请求、读回和恢复合同；仍不能据此声称游戏采用了对应 SR/FG、实际倍数、
  性能或长期稳定性。真实 40/50 游戏与 GPU 验收仍由维护者集中执行。

## Changed paths

- `main.js`
- `src/product/launch-coordinator.js`
- `src/renderer/renderer.js`
- `test/launch-coordinator.test.js`
- `test/launch-settings-frontend.test.js`
- `docs/handoffs/2026-09-08-manager-line-c-hotfix6-launch-failure-recovery.md`
