# Beta 4 GPU 成本与资源记录

源码固定为 `85b59bdc89a23db14f06d911e83a26a385af351c`，实际生成 r6 Core，RTX 5090 D、同一 Kio NR 库。测试期间没有并行的代理 GPU 验证任务，也没有游戏操作。以下数据属于受控人工宿主，不能当作游戏帧时间或画质验收。

**4K 时域成本的基础 NR 和新增处理均出现高低两档波动。原始结果全部保留，尚未证明稳定性能或整游戏帧新增 ≤3%。** 不依据各组 NR 耗时差计算“提速”，也不挑尾段或较快样本代替全窗口。AMDOF 继续作为默认关闭的手动实验；原生 MV 与低频方案也未取得整帧预算通过结论。

## 私有 SR：三场独立 32 帧

每场完成 32 个最终 Z，其中首次创建保留 Z，随后 31 次私有推理。产品统计排除前八次推理，保留 23 个样本。下面“私有区段”是外围 GPU timestamp，包含该调用区段；导引和完整 NR 链分别记录。产品内部计时只覆盖私有工作，两种口径不混用。原生 SR/RR GPU 时间及整游戏帧均不在此表中。

| 原生路线 / NR 层数 | O / N | 私有区段 median / P95 ms | 导引 median / P95 ms | NR 链 median / P95 ms | 自有 planes 峰值 → 退休 |
|---|---|---:|---:|---:|---:|
| SR / 1 | 640×384 / 320×192 | 0.176448 / 0.177376 | 0.006080 / 0.006560 | 2.114624 / 2.123808 | 12.25 MiB → 0 B |
| SR / 3 | 640×384 / 320×192 | 0.178176 / 0.223040 | 0.005984 / 0.012960 | 6.219872 / 7.437408 | 12.25 MiB → 0 B |
| RR / 5 | 1920×1080 / 960×540 | 0.343040 / 0.346560 | 0.013504 / 0.014112 | 11.229632 / 11.264416 | 114.06 MiB → 0 B |

首次创建与首次推理 GPU 区段、产品内部 median/P95、逐帧 CPU 录制/提交/退休耗时及 DXGI 局部使用峰值/回落，均见[原始回执](D:/ChatGPT/DLSS5/evidence/beta4-final-private-cost-r1/validation.json)。这些创建 GPU 区段不等于 CPU NGX Create 的单独耗时。自有 planes 退休为零不表示 NGX/驱动缓存已经全部释放；DXGI 口径与自有资源口径分别保留。

## 时域方案：完整 4K 原始结果

以下报告包含所有新增光流、转换/拷贝和累积工作。原生运动输入在该夹具内已是零起点、归一化 N 网格；它没有覆盖游戏边界上任意 jitter/区域映射转换的额外成本。因此不作为完整原生接线整帧预算的替代。普通源图构造/上传与最终图像采集位于计时段外，关闭组不计算参考光流。

# Final r6 controlled 4K temporal cost

**Timing-stability audit: these windows are strongly multimodal; no stable product-cost conclusion is established.** See `timing-distribution-audit.json` for all 32 values per group.

Eight functionally successful cases; O=3840×2160, N=1920×1080, real Kio Feature18 with 1 or 3 layers. Each process excludes its first eight complete NR frames and reports the following 32. GPU window was coordinated as exclusive. No games were run.

| Layers | Mode | NR median / P95 ms | Additional median / P95 ms | Total median / P95 ms | Accounted incremental MiB |
|---|---|---:|---:|---:|---:|
| 1 | off | 44.5701 / 45.5499 | 0.0001 / 0.0001 | 44.5702 / 45.5500 | 0.00 |
| 1 | native MV / Adaptive | 44.6264 / 46.3072 | 13.0357 / 13.2164 | 57.7240 / 59.4569 | 430.54 |
| 1 | AMD OF / Adaptive | 44.5020 / 44.8750 | 13.1303 / 13.2972 | 57.6445 / 57.9876 | 376.31 |
| 1 | AMD OF / LowFrequency | 44.4648 / 44.7692 | 13.0605 / 13.2259 | 57.5393 / 57.9337 | 376.31 |
| 3 | off | 53.6927 / 63.2654 | 0.0001 / 0.0037 | 53.6927 / 63.2654 | 0.00 |
| 3 | native MV / Adaptive | 11.3840 / 63.2648 | 0.6614 / 7.0475 | 12.0447 / 70.2908 | 430.54 |
| 3 | AMD OF / Adaptive | 53.5682 / 64.1552 | 6.0936 / 7.2277 | 59.6668 / 71.3997 | 376.31 |
| 3 | AMD OF / LowFrequency | 11.3858 / 63.4277 | 1.0500 / 7.2257 | 12.4344 / 70.6431 | 376.31 |

All accounted allocations are below the product 768 MiB temporal limit, although the fixture admission budget was 1 GiB. Accounting includes module-owned resources, SDK-reported allocation and native N-guide textures/upload footprint; it excludes shared baseline NR, opaque NGX/driver allocations and total process VRAM.

The additional interval is measured immediately after the same frame’s complete NR work, including all temporal-specific GPU copies and actual accumulation/flow work. Common artificial scene upload and final image readback are outside the measured interval. Native inputs are artificial already normalized zero-origin N-grid guides; this is not timing of arbitrary native-jitter normalization at a game seam.

NR timing varies substantially between groups and inside the short three-layer windows. Do not subtract cross-group totals or credit a faster NR sample against added work. The table therefore presents paired per-frame added time alongside each group’s NR time. No cause of the variation is established.

These measurements do not provide a full-game-frame denominator and do not establish the ≤3% target. AMD OF is too costly here to qualify as a demonstrated low-overhead common default; retain it as an optional manual experiment pending optimization and game evidence. Native MV and low-frequency quality/performance acceptance also cannot be inferred from this cost-only fixture. No repeated runs were used to cherry-pick results.

All eight final FP16 captures and source/runtime/object hashes are retained in validation.json and per-case folders. Compile-only receipt remains separately preserved.

## Distribution audit (additional GPU ms)

| Layers | Mode | Min | Median | P95 | Max |
|---|---|---:|---:|---:|---:|
| 1 | 0 | 0.0001 | 0.0001 | 0.0001 | 0.0068 |
| 1 | 1 | 6.9472 | 13.0357 | 13.2164 | 13.2204 |
| 1 | 2 | 6.0621 | 13.1303 | 13.2972 | 14.1567 |
| 1 | 3 | 6.1068 | 13.0605 | 13.2259 | 13.2427 |
| 3 | 0 | 0.0001 | 0.0001 | 0.0037 | 0.0041 |
| 3 | 1 | 0.6512 | 0.6614 | 7.0475 | 7.0667 |
| 3 | 2 | 1.0393 | 6.0936 | 7.2277 | 7.2444 |
| 3 | 3 | 1.0431 | 1.0500 | 7.2257 | 7.2418 |

The single-layer AMD tail is about 7.2 ms, while 19/32 samples occupy the slower NR band and push the full-window additional median to 13.1303 ms. This is not a parser discrepancy. Three-layer native has 18/32 samples in the faster band; three-layer AMD has 18/32 in the slower band. Median selection therefore changes abruptly with cluster occupancy. Raw clock frequency values, clock/power telemetry, scheduling/residency traces and per-frame adopted counters were not logged. The cause cannot be established retrospectively. CPU image generation/upload is before timestamp 0, the temporary common upload is released after its exact completion, temporal resources retain constant accounted bytes across each run, timestamp readback follows query 2, and final image capture occurs only after all 40 produced frames.
