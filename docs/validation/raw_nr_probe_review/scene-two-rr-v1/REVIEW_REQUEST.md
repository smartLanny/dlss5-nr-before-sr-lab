# 同源3D twoRR v1 冻结包末核

仅CPU准备，GPU/NGX未运行。原SceneFrame/renderer/CSO/矩阵/七planes/oneRR本体保持成功measure-r2原bytes；固定raw alpha1(0x3c00)，不搬flat.75、不泛化finite、不改scene输出。真实源码差异见CRITICAL_SOURCE.diff，核心接缝已获Astra源审CPU GO。一次CPU拼接边界编译失败已保留并精确恢复原helper，Finish外原bytes完全不变；修复亦获Astra窄核后二次CPU编译，未发生GPU运行重试。

请末核新root-only唯一3帧runner及receipt：fresh/no retry/no kill；严格functional/nron/2warm1observe，其余CLI在device/bootstrap前拒绝；parentCreate+childCreate+三帧=5真实recording、初始parentCreate内Scene dispatch1+每帧3=4。24中间、21实际输入、3个O完整实际hash/readback，48byteconstants/实际native token-reset源追溯；每帧child/NR/parent独立计数，同原domain不重Init、raw guides明确approximate、同真实queue/fence/value完成+Reset退休。

frame2必须实际combine且预测/实际RGB都非零改变；变化仅在oracle合法域，合法值沿原<=1FP16ULP容差，全alpha逐u16和nonedit完整RGBA bits相等。accepted预测域与actual changed分开，不能冒称GPU写掩码。任何零覆盖/首失败停止，不能预设功能/画质收益或扩大到capture/perf。

SOURCE_HASHES保private/source SHA、sanitized SHA与Git LF内容SHA；仅自有代码/窄diff/CPU摘要，无vendorSDK、binary、原始日志或真实绝对路径。root在最终GO后单独串行执行；本代理不GPU/不派生。质量/性能/游戏unknown。
