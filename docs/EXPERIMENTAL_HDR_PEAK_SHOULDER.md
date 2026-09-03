# Experimental HDR highlight detail

This option is **off by default**. Enable `ExperimentalPeakShoulder=1` in the
INI, or use **Experimental HDR Highlight Detail** under Advanced / Recovery.
It is an experiment for controlled comparisons, not a verified improvement
for a particular game. Restoring recommended settings turns it off.

The existing HDR proxy first compresses luminance and then divides RGB by its
largest channel when that channel exceeds 1. Along some fixed RGB colour
directions, this makes different bright values identical before NR sees them.
At unit normalization, `(2,1.2,0.8)` and `(4,2.4,1.6)` both become `(1,.6,.4)`.

The experiment replaces those two steps with a peak shoulder. For normalized
nonnegative source RGB `d`, let `P=max(d)`:

```
q(P) = P                        when P <= 0.75
       1 - 0.0625/(P - 0.5)     when P > 0.75
proxyLinear = d                 when P <= 0.75
              d*q(P)/P          when P > 0.75
```

The low-peak region is unchanged and each positive RGB colour direction keeps
its brightness ordering before quantization. The example above retains peak
values 0.958333 and 0.982143. Values remain bounded; existing sRGB encoding and
FP16 proxy storage follow this mapping. Negative source channels keep the
existing proxy clamp-to-zero policy. Invalid experimental proxy values are
neutralized; the raw original validity check and composition fallback remain.
This is not an invertible representation of all signed HDR values, and FP16
quantization still limits extremely bright detail.

Only the already-admitted Automatic HDR and PQ proxy paths use the option.
Linear SDR, sRGB SDR and model-encoded inputs do not use it. A game with SDR
display output can still supply a scene-linear HDR input to DLSS; the live
input contract, not the display switch alone, determines whether it applies.
Exposure normalization, paper white, colour admission, colour following,
composition ratios, alpha and native SR settings are unchanged.
Changing the setting can still invoke the common configuration reset and
warmup even when the effective SDR mapping remains unchanged.

Changing the effective proxy mapping retires the current Feature 18 after its
exact GPU fence, creates a new instance and restarts warmup. It cannot reuse
the previous mapping's temporal history, including when toggling off again.

For A/B testing, hold the scene and all other settings fixed. Use F7 after
settings apply and warmup completes. Stage metadata records the **effective**
`proxyMapping=existing` or `proxyMapping=rational-peak` (`codecFlags` bit 256).
Instance logs record the same mapping. The `-` screenshot filename records
**requested** `HP0` / `HP1`; it remains separate from the NR frame identity.
Compare Original and Proxy first to verify the affected region, then Neural
and Composited, and finally motion. A numerical mapping fix does not establish
better faces, model response or temporal stability.

## 中文操作说明

“高级 / 故障恢复”中的**实验性 HDR 高光细节**默认关闭，对应
`ExperimentalPeakShoulder=0`；设为 1 才启用。恢复推荐设置会再次关闭。
它用于对照高光输入纹理是否在进入 NR 前被压平，不能据此宣称某款游戏画质已经改善。

保持同一镜头、光照和其他参数，切换后等待 NR 新实例预热完成，再按 F7 采集。
metadata 的 `proxyMapping` 是实际映射；截图文件名的 `HP0/HP1` 是请求值，
两者不是同帧证据。只有现有 HDR/PQ 代理路径使用实验映射；SDR 编码映射不变，
切换设置仍可能触发通用历史重置与预热。
游戏显示为 SDR 时仍可能向 DLSS 提供场景线性 HDR，须以实际输入合同和 metadata 为准。
实验只改变代理映射；不扩大颜色准入，不修改游戏 SR、曝光来源或默认合成。
