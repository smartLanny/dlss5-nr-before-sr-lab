# 新 RR 的 OptiScaler multipass 对照 — 2026-10-08

维护者指定参考 [wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass)。
已实时核对代码和发布身份，当前有关新两RR实现的是仍开放的
[PR #123](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/pull/123)，不能说已合 main 或已在最新发布包中。

| 对照 | 实际固定身份 |
|---|---|
| main | `1bd39091337cc07ba961c8e59ded57e21dc95b18` |
| 最新相关 prerelease v0.8.91 | `f45ccf3a761df91450959d325ac0166cba5364c9`，compressed-input preview |
| 新 denoise-first PR #123 | `8fbdfa60df631c1f41bd39cf6afcbd96d76f1979`，base=f45ccf3a，仍 open |

读取固定源的 `DlssNr_Dx12_DenoiseFirst.cpp`、`DlssNr_Upscaler_Dx12.cpp` 和
`precompile/dlssnr_denoise_first.hlsl`，只研究设计与可验证假设。没有下载/安装其
二进制，没有复制 GPL 源码、运行库、配置或 shader 到本产品。
浅克隆只作精确固定树内容对照，不用来判断缺失历史或分支祖先。

## 对当前路线的帮助

核心路线与本单已选方向一致：raw→独立同网格RR→NR(clean)→将编辑移回raw
采样位置→原生RR放大。继续采用比值先形成、再移相的 P 候选：
`X * (1 + T(A / C - 1))`。先分别插值 A/C 的 S 候选及绝对差 D 保留为开发
反例，不混写为同一算法；identity、所有正权 tap 合法及原始位型退回均独立验证。

借鉴其对 clean、NR(clean)、composite 和最终结果的分阶段采集，以及将额外
RR 成本单独显示的做法。增加一次RR的代价必须与相同NR分辨率/层数的普通
恢复对照，不能只与4K NR比较后宣称整机帧数提升。对方的游戏观察、成本和
局限是第三方证据，本项目尚未复现，不并入本Core验收计数。

## 接入时的具体边界

- 参考实现将 clean 视为稳定网格，并提供 REDengine 经验性的 jitter 方向选择。
  本Core仍须证实际 clean 相位、HDR尺度及导引对应，不根据相关性最大或最清晰
  的图像在线选方向。最新官方Sample/NRI直接设置链可用于明确的受控fixture，
  不因此宣布所有游戏合同已知。
- 参考调用 NR(clean) 时仍传原始 depth/MV。本Core不把资源存在或宽高相等当
  clean-grid导引已证明；单平面oracle、非恒定depth/运动、遮挡/显露须分别验证。
- 参考shader含固定分母下限、gain上限、边界clamp/重归一化、可选高阶kernel/
  邻域/亮点保护。暂不搬这些选择；先验证最小正权双线性及明确拒绝域，只有
  具体反例支持的修复才进候选，不为了保留功能继续堆保护。
- 参考可在RR导引不足时改用私有SR。本单当前验证的是实际Feature13同网格
  能力，不能用Feature1成功替代，也不恢复已停止的私有SR重建候选。
- 保留本Core已证的当前init/parent域、opaque命令状态、immutable输入、精确
  queue/fence＋recording退休和release-before-create；不照搬别的项目epoch/
  retired generation策略替代这些证明，不增加第二提交账本。

界面继续一个清楚的实验入口和必要设置，解释放短悬浮提示；不复制参考的
整组实验开关。现阶段默认产品仍RR→NR，新两RR未接产品。原issues、最终
组合canonical、统一游戏测试及Claude main所有权保持原计划。
