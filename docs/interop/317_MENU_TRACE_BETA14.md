# Beta 14: Esc transition evidence / Esc 过渡取证

## Scope

Issue #317 remains open. Existing key-time snapshots do not establish whether a
short NR interruption follows an input-size change, guide storage/mapping change,
history reset, model recreation, or a different output path. This change records
bounded events around Esc and includes them directly in the local F8 text report.
It does not change rendering admission, resource lifetime, history policy or model
size selection. Size hysteresis is deferred until the relevant transition has been
observed; a future candidate must remain off by default pending game acceptance.

The package identity is `0.5.2-beta.14`. Released Beta 13 artifacts keep their old
names and identities. CPU/WARP and successful calls do not prove displayed NR
pixels or game acceptance. Evidence gaps and dropped records must stay explicit.

## Owner check

1. Keep the same scene, native SR/RR mode, layers and settings. Leave experiments
   as saved; do not change configuration during the comparison.
2. Open and close Esc three times, allowing each transition to finish. Observe
   whether NR disappears while the camera and UI move, and whether it returns.
3. Press F8 afterwards. The text report should retain the transition records,
   including the input and model grids, guide-contract changes and model/history
   events. A zero-event, incomplete or overflowed window is not a healthy result.
4. Note which transition visibly lost NR. Final-pixel adoption remains a human
   observation; submission counters alone do not establish it.

No nine-group benchmark is needed for this diagnostic step. Persistent bypass,
unexpected configuration changes or a crash are regression failures. This is not
a claim that #317 has been fixed.

## 中文说明

本轮先补齐 Esc 过渡的证据：两次离散采样之间发生的短暂变化，也应进入同一次
F8 本地文字报告。记录需要分清输入与模型尺寸、导引存储和映射、资源轮换、
模型创建/退休及历史 Reset；资源指针轮换不能自动当成重建原因。

画面处理规则保持原样。尺寸滞回暂未加入；只有观察到与故障一致的变化后，才
评估该候选，且须默认关闭。此 PR 不关闭 #317，不新增画质或帧率改善结论。

维护者只需固定场景和设置，进出 Esc 三次，随后按 F8，并标明哪次看到 NR
消失。无需重新跑九组。事件缺失、记录溢出或调用增长都不能代替最终画面证明。
版本已先更新为 Beta 14，未修改维护者保存配置。
