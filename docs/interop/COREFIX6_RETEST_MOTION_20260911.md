# Corefix.6 复测：运动矢量轮换时的槽位选择缺口

维护者确认绝区零前置、后置仍强烈闪烁。反馈内冻结源为
`04bb64fa64b8020a21f8283f5d99cfc581cdbc12`，加载的中文 Core SHA-256 为
`d1a0ddc02884e06309fe9b46f45e08085db39aaf133ef5f1c876ca7084539ce9`，
与 Corefix.6 已交付产物一致。本地验收通过不改变这次游戏失败结论。

## 当前窗口证据

F8 起止均为 config 7、epoch 2、Feature generation 5、mode 1。carrier Evaluate
从 1237 增至 1281，NR 成功和已提交 transfer 各增加 14；最近有界记录同时包含
有 NR 和仅原生 SR 的调用。这些是调用计数，不是 FPS、Present 或完整显示帧轨迹。

尾部约 0.7 秒内有 27 条 Motion 描述符修改被延迟的日志，明确说明旧 GPU 使用
尚未结束，并保留原生 SR。相同窗口没有增加未知队列旁路、队列预测旁路、实际队列
不匹配、marker 失败或退休等待；近期 timing 的 resetReasons 为 0。

CPU Clear 格式候选在起始快照通过当前调用 gate；终止快照的 gate 为 not-required，
不能把它解释成拒绝或第二次成功。较早配置切换期间仍有 unsupported-input/output
记录。当前材料不能证明每次调用都完成格式解析，也不能把这些旧记录替代尾部直接
报告的 Motion 旁路原因。

## 已确认的源码反例

`FindOrCreateSlot` 先按 Color/Output 和 exposure 找到第一个槽位，未考虑本次
Motion 绑定。若该槽已经绑定 Motion A 且仍被录制列表或 GPU 使用，本次请求
Motion B，后续 `EnsureMotionProxy` 会正确拒绝改写描述符；其他空闲槽位没有机会
被选中。这是一个可以单独复现的不必要 NR 旁路。

前置原始源码和经 `scripts/patch-post-compat.py` 组装后的后置路径都调用
`EnsureMotionProxy`。只读原始后置源码会漏掉生成阶段插入的调用。

安全修复应在同一 `NrContext` 内优先选已有正确 Motion 绑定的槽位，再选可安全
修改的槽位或空槽。保持 8 槽上限、同一 NR handle 和 temporal history，不放宽
录制引用、待发布 fence、精确完成 fence、队列或资源格式检查。所有槽位都不满足
条件时仍须保留原生 SR。

## 验证边界

日志直接证明 Motion 修改被拒绝以及 NR/原生 SR 调用交替；源码反例解释了一种
具体触发机制。反馈没有逐次导出所有槽位占用情况，因此尚不能断言每条旁路都由
首槽抢先命中造成，也不能断言它是所有画面闪烁的唯一原因。

后续候选须通过生产选择路径的回归和实际 WARP 资源/完成 fence 检查，再交付
维护者在相同场景复测。真实 NVIDIA NR、最终显示及性能仍分别待验。
死亡搁浅既有无持续效果和偶闪问题保持开放，本次不要求重复其四阶段测试。
