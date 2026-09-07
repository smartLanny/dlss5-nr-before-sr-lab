# Null-guard：有限 blend 值准入（2026-09-07）

基于 `origin/main@ba71b45`。这是 command-state 准入的最小修正，不是宣称异环 RR / 法环后置 / 沉没之城2 后置已实机修好。

## 证据

当前 main 已允许 `dynamic=0x8`（仅 `OMSetBlendFactor`）走 guarded-null-raster。仍用 `[0,1]` 作为额外策略，而不是 D3D12 API 规则。

对照用、不并入验收的 #101 王牛奶 `beta0.4.5-multiapi-dev3` F8（`260907-htgame-beta0.4.5-multiapi-dev3-204535319-40728-1.txt`）给出同次实测：

- `commandState.reason=null-guard-graphics-state-used`
- `dynamic=0x8`，`freshReset=1`，`tainted=0`，`nullBaseline=1`
- `blendObserved=1`，`blend=-0,-1,-2,-3`（start / end / 12 条 history 一致）

`#64` / `#38` 的 20260907 fix1 后置同样是 `dynamic=0x8` + covered/freshReset，但旧包未导出四值。`#123` leisure 是 `dynamic=0x46`（viewport|scissor|targets），缺当前包 F8，标 needs-retest。

树海 #99 旧 inbox 是 `beta0.4.4-test1` 前置成功，不能当 multiapi-dev3 RR 终态。#99 dev3 摘要（PR #149/#150）仍是 mode=1 / workMode=5、scope.nrSuccess=0、RR recordings=0；原 TXT 不在本树。

## 修改

`CanAutoRestoreRasterNull` 对已记录的 blend 只拒绝 NaN/Inf。观测到的有限值原样 `OMSetBlendFactor` 回放，不夹取、不改游戏值。Hook / unknown / taint / fresh Reset / 其它 dynamic 位规则不变。

## 边界

- 不把 #101 并入 #99/#15/#53/#97/#100 验收。
- 不放松 viewport/scissor/RTV 私有描述符规则。
- 不改 RR 原生调用、资源或生命周期；RR 边界计数仍看 PR #150。
- 启动瞬间 `native-hooks-unavailable` / `installing` 仍不是终态。

## 验收

待 ROG 用已发布 A 包或已发布 `multiapi-dev3` 复测。需要同次 F8 的 `commandState.blend` 四值、scope 成功/录制/提交，以及 #64/#38 前→后、#99 普通 SR 与 RR。云端未编 Windows OTA。
