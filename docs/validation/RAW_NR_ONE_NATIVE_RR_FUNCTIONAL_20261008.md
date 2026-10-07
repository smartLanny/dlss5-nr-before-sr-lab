# raw低分NR→一次原生RR：受控端到端功能结果

2026-10-08。按 Astra 最终窄核，只运行现有私有宿主的两条固定独立历史，均
一次 exit0，没有重试或参数扫描。**受控功能切片通过；未接产品，未证明真实
3D/游戏画质、性能或兼容性。** 两 RR 的 clean-guide producer 阻断不变。

## 固定合同与实际结果

R1280×720→O1920×1080；RR MaxQuality2、质量专属 preset Default0、flags75。
NR 仍为 N=R、生产 binder Quality5/intensity1.5、既有 curve2/scale1/strength1；
SDK19、旧310.5 native tuple 和 a2d16 NR 不变。NRD 不参与。

bootstrap 后，实际 original Core capability 导出及官方 optimal callback 的模块
owner 保持有效。真实 cap/optimal 在唯一 parent Create 前精确确认目标 O 对应
R，不造 dummy parent/child RR，也不扫描尺寸。两组使用相同 J 输入、raw guides、
P0、j=-s、无 jitter 物理 MV、曝光与 Reset 日程，各16warm/16observe。

| 历史 | 原生 parent Create / RR Eval | F18 Init / Create / Eval | 精确退休 recording |
|---|---|---|---|
| baseline：raw→一次RR | 1 / 32 | 0 / 0 / 0 | 33 |
| variant：rawNR→editedStored→一次RR | 1 / 32 | 1 / 1 / 32 | 33 |

baseline 不携带 NR DLL。variant 用实际生产 F18/codec，原始 Color 经真实
typed/void alias 核验后仅替换为 R-sized editedStored，成功调用后核验并恢复。
所有非 Color aliases、subrect、scalar、P0/CPU owner 前后重新读取且相同。
每帧仅一次原生 RR，Output 是独立 O-sized 资源，未叠外部时域或私有 Feature13。

NR 原三份 lease 与新增 RR consumer 同真实 recording/token，依赖持续活到
RR 消费及同队列精确 fence 完成、成功 Reset 后才共同退休。成功释放路径及
真实模块/device/domain 有收据。释放路径依据准确源码/EXE 的 return-success
assertions、末尾 PASS 和实际 NR owners-release 事件，不声称另有逐项 SDK
Release 原返回日志。失败沿原 quarantine/flush/_Exit，不提交失败
recording、不释放不确定闭包、不重试或回落；本次没有触发厂商故障分支。

## 输出证明与限制

每帧同一实际 O 资源先由 immutable upload-copy 全面写入 FP16 e4d2 sentinel，
再调用一次 RR，同 recording 记录读回，精确完成及 Reset 后才 Map。64个输出
均为真实 1920×1080 R16G16B16A16_FLOAT，紧凑 stride15360、每帧16588800字节。
全部398,131,200个 RGB 分量有限且不留 sentinel，文件/SHA/实际 row pitch/offset
均保存。alpha 字节保存但完整写入合同仍 unknown，不把 RGB 结果外推为 RGBA。
variant 的160件原 NR/codec 中间附件保留用于追溯。

baseline/variant 差异仅作 diagnostic，没有强制差值或画质优胜阈值。这是一对
不同颜色历史，不是 paired repeat 测量。先前四 NR 历史的有限 c=s 相位门继续
独立成立，不据本次功能成功宣布连续相位、复杂材质/遮挡或 HDR 画质通过。

## 源码与证据身份

实际 Core 来源仍为本地组合 `45df68dc80138e89778e544d49f15109ec437a4f` / src
`7fe3819be240c0fad0eb16526e9d4b3ce0a67f87`。核337件组装来源（311 Core、
26 carrier）、24链接对象及实际 compiler/TU；不是当前 #342 文档 head 二进制。
完整关键 diff 只改私有 probe，不改生产 Before/Feature1 门或游戏配置/运行库。
见 [审查副本与范围](raw_nr_probe_review/one-rr-functional-v1.README.md) 及
[完整自有关键diff](raw_nr_probe_review/one-rr-functional-v1.diff)。副本仅 LF
规范化，不是独立可构建包或 EXE 完整来源。

| 封存项 | SHA256 |
|---|---|
| 执行包 manifest，46件 | `a3eb6beccfdbb3cbbf527134d12f3422d89ff1d4d2cc21ce603a4acd1cf13d2a` |
| 实际 EXE | `d9581ec77588aaae84ce5be492ca9c39617f0d6aa855042ba9e2896b679a5b83` |
| 原关键diff | `f01ba7901c350294ed27b070d4d333fc7caebe37e546bb7c57e0a35b4ffa3a1a` |
| Astra 最终窄核 | `15f3004c9cab0c30b30f2795f33b5f889d7e9901e58896f9cc9b51dfaecd0bea` |
| 实际结果 manifest | `1fd2cb8b1fb2b8198e0634801412255467b0d144fb64cef965f1fac7f2a8d95e` |
| 实际 validation | `e2c2f1611fe43e52a9c679d54c6a336f5a95f12fa4d734ba64a19f65eeddbc67` |
| 实际 runs | `703fc8f2f0070f8651ff5c5395473202cab5eeae412bad132afd762422aa1f8c` |
| 外层监督 manifest | `1d350bb3295efa028c7935570db68d3c5463a0f419ebe26572afd8f9340fa8b4` |
| 独立 CPU 实际收据 | `531b6789c0d750af7fbd968e38c72816ec7b7c7ad3324d4a2da65eb1a047ce45` |

独立 CPU 复核实际252件文件 size/SHA、冻结46件及监督6件，从冻结 checker
重算两进程所有收据，与 runs 逐项一致。64输出398,131,200 RGB 逐位扫描通过；
66精确退休配对、同源输入/P0/raw guides/实际 Color 事务及160件 NR 附件的
SHA/roots 均核回。未重跑 GPU，没有把不同两臂称 paired-repeat 或画质通过。

外层40次 known-process 检查没有检测到并行已知 GPU 实验/游戏；没有启动或关闭
游戏。它不是所有 GPU 客户端的全局测量，更不是帧时间/显存性能测试。

本次结束此固定可行性切片，不再扩故障矩阵、控制图或另建 bench。产品接入、
真实游戏输入合同、3D画质/性能及厂商 partial-handle/device-loss/failedReset
仍未知。官方 NRD-Sample 适配本批 STOP/noMVP；两 RR 缺真实 clean producer，
不能以此一次 RR 改名完成。现有修复组合的 canonical/维护者短测独立交付，
不因此启用游戏新 RR、声称私有 SR 消闪或0.4.7性能差已修。
