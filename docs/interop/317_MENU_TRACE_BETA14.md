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
Within each Esc window, the first pointer-only resource rotation for each
carrier/route/layer/source-stream identity gets one event. Later pointer-only rotations update
that event's count, last time and union resource mask; the window also reports
the full rotation count. Frame, guide storage/mapping and lifecycle changes
remain separate events. Repeated pointer-only rotations do not consume more
ring capacity; genuine new events can still overflow, and an evicted rotation
still contributes to the window total.

F8 trace text has a separate 24 KiB allowance and leaves 32 KiB for later worker
metadata. It emits at most eight detailed events per window, reducing that number
when space is tight; the opening press and contract/lifecycle records take
priority. `eventCount`, `eventOutputCount` and `eventOmitted` distinguish retained
events from serialized detail. Ring overflow remains a separate field. Capacity
or staging-memory failures have explicit status markers when the marker fits.
A complete report does not imply complete event coverage.
The shared F8 metadata buffer is bounded to 288 KiB; the final TXT is bounded to
480 KiB so metadata can coexist with six existing bounded log summaries. These
are CPU buffers used for collection, not GPU allocations or normal-frame queries.

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
同一 Esc 窗口内，每条 carrier/路线/层/来源流仅保留首个纯资源指针轮换事件；后续
纯轮换合并到该事件的次数、末次时间和资源字段掩码，并保留窗口总次数。
尺寸、导引存储/映射及生命周期变化仍逐项记录；只有实际新增事件会占用
有界环并产生真实溢出。轮换事件若被其他事件覆盖，窗口总次数仍保留。

F8 的 Esc 文字单独限额 24 KiB，并为之后的身份与状态字段保留 32 KiB。
每窗最多输出八条详细事件，空间不足时减少；优先保留首次按键和合同/生命周期
变化。`eventCount`、`eventOutputCount`、`eventOmitted` 分别说明环内事件总数、
输出数量和省略数量，环溢出另列。容量不足或临时内存不足会在可用空间内明确
标记。报告写完不代表所有事件细节都已输出。
共享元数据缓冲上限为 288 KiB，最终 TXT 上限为 480 KiB，为六路既有日志摘要
保留空间。这些是采集时的 CPU 文字缓冲，不增加 GPU 分配或普通帧资源查询。

画面处理规则保持原样。尺寸滞回暂未加入；只有观察到与故障一致的变化后，才
评估该候选，且须默认关闭。此 PR 不关闭 #317，不新增画质或帧率改善结论。

维护者只需固定场景和设置，进出 Esc 三次，随后按 F8，并标明哪次看到 NR
消失。无需重新跑九组。事件缺失、记录溢出或调用增长都不能代替最终画面证明。
版本已先更新为 Beta 14，未修改维护者保存配置。
