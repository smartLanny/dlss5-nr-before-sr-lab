# 安装器反馈补充核对（2026-09-07）

输入为维护者提供的本机bug-reports/_sheet_installer/INSTALLER-BUGS-2026-09-07.md、_raw_scrape_2026-09-07_1713.json与chat_people.json。未复制聊天原文、昵称、账号或个人信息到仓库，也未向群聊/反馈表发送消息。

材料含五类有效问题，缺少安装器诊断导出、实际payload清单、组件SHA256、内嵌版本和源码提交。安装器文件名、安装器本体版本、所选ADDON版本、安装后实际核心版本是四件不同的事；0.4文件名不能证明核心不是0.2。以下为历史比对，不是复现结论，也不据此关闭Issue。

核对独立Manager分支product/xiaofeng-manager-foundation@ce6dae6。bdc4b24与09e66f3已证实是该提交祖先；Manager未混入本次兼容核心main。

| 反馈 | 已有源码证据 | 本轮判断 |
|---|---|---|
| 选择0.2/0.3却装成0.4.2 | bdc4b24引入版本化payload；version-selection.js优先显式操作，其次游戏锁定，最后全局选择；DX11专用路线例外 | 相关机制已修，报告实际旧包身份不明，仍待确认同因 |
| 缺少组件/不知道组件在哪 | payload.js核对组件存在与SHA，返回ERR_PAYLOAD_MISSING/ERR_PAYLOAD_HASH以及missing/invalid清单 | 当前有诊断和拒绝机制，不能据无日志口述新增根因 |
| portable/0.4.x进入游戏崩溃 | 报告标注beta2-portable/0.4.4test，无崩溃日志与组件身份 | 历史兼容性线索，不能认定dev4新增回归或已经修复 |
| 更新/应用版本异常，操作后仍报错 | 09e66f3修复操作成功但刷新/IPC状态导致界面报失败；错误仍正常上报 | 可能已修复，尚未证明报告使用含此修复的安装器 |
| 2077只有旧版能运行 | 原材料仅有驱动相关推测，无安装清单或故障证据 | 保留驱动/游戏兼容线索，不把推测写为根因 |

本轮在干净Manager ce6dae6检出实际执行 `node --test test/version-selection.test.js test/action-flow.test.js test/payload.test.js`，11项通过，无skip。覆盖显式版本覆盖游戏锁、游戏锁优于全局、DX11专用选择、缺失/篡改组件、GPU版本选择，以及成功操作后刷新失败的状态保留。这些测试不是受影响用户实机复现。

未来有新证据时优先使用安装器本体版本、requested内部payload ID、安装后manifest实际payload版本和逐文件SHA/PE版本，以及操作结果/缺失清单；当前已有资料不要求重交。当前F8同时记录实际build/sourceCommit和addonSha256，可辅助区分旧包与新包，文件名仅作线索。
