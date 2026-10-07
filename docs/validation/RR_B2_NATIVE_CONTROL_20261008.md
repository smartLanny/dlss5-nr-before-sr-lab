# #309 B2：完整 Core adapter 与原生 RR 控制

本报告只交 B1/B2 接入审查。没有接入产品 Before、没有 private SceneRR，
没有运行 NR 或实现 Denoise-first；原 RR→NR 和冻结 #338/#340 保留。
main/base `024f9cc32778c1056db364d76d087ee257da6502`，实际编译 Core 为
`9b2636eb6cd80b540f80194fc6be771d14709c0b`，不是当前分支或组合构建。

## 完整 Core adapter

`validate-rr-identity-core.py` 验证实际 source manifest、source-defines、
ABI headers 和链接对象，完整编译 Core。真实 NativeCommandStateScope、
Create/setter/Reset observer、AcquireBindingPacket、RecordNrUseLocked、
CanMutateSlotLocked 和 WARP Execute/Signal/exact fence 组成 adapter。
七种场景 282 项通过：52 lifetime、53 failed-reset、32 unsubmitted、29
state-refusal、32 opaque-failure、30 off、54 replay。

移除实际 recording 发布／scope Restore 两项副本各编译成功后，在
`LEASE_PINNED_BEFORE_FIRST_COPY`／`PRODUCTION_ENVELOPE_RESTORED` 精确
exit 41。可重执行 list 即使 GPU 完成仍保持 lease；未提交的 opaque
consumer 不使用 copy-only discard 回收。冷 Collect/Retire 保持 chain
锁，copy-read-state 查询保持 source 锁。该部分 native consumer 仍为受控
copy，没有 NVIDIA 推理；现有实际 command-state 宿主另覆盖 ClearState。

## 隔离原生 RR 对照

三个进程分别为 A 原生、A2 原生重复、B Color identity；每进程一次
bootstrap、一个原始 Feature13、32 次 Evaluate（16 暖机＋16 观察）。
Feature1/18、NR、私有场景 RR、产品入口和游戏调用均为 0。

- R=N=1280×720，O=1920×1080，FG off。N=R 不要求 O=R。
- 真实 HDR 存储为 `radiance * .75 / 2`，preExposure=.75、exposureScale=2。
  Flags=75 含 AutoExposure；没有声称读取到模型内部曝光。
- 控制场景为不透明静态平面：+Z 单位 normal、线性 albedo、roughness .5、
  primary/specular MV 为 0、反向无限远硬件 depth .05、near .1、view z=2，
  配套相机矩阵。jitter 为 (0,0) 输入像素。透明及可选 ColorBefore/After
  明确为空。CPU 上传来源 SHA 并非另做七平面 GPU 回读。
- 三组 32 帧 Color、七导引、scalars、getter 和源身份合同一致。独立 CPU
  重建的全部 32 帧 Color SHA 与上传来源相等。
- B 的 32 帧 copied/substituted/restored 均为 1，实际 Core 租约 acquire/
  release 各 32，沿真实提交、精确 fence、成功 Reset 退休。A/A2 租约为 0。

| 16 观察帧完整 RGBA FP16 | RGB max / MAE / RMS | Alpha max | 变化成分 |
|---|---:|---:|---:|
| A–A2 原生重复 | 0 / 0 / 0 | 0 | 0 |
| A–B identity | 0 / 0 / 0 | 0 | 0 |

这是本次受控序列的观察，不是通用 NGX 逐位确定性合同。全部 48 个观察帧
各通道均有 2,073,600 个有限非零成分；RGB 全局范围为 R 1.16015625–
1.9345703125、G .99951171875–1.6513671875、B .78173828125–1.259765625。
每帧有空间梯度，相邻观察帧均有变化。未预写 sentinel，不能据此声称逐像素
独立写掩码证明。没有画质优势、总成本或 FPS 结论。

复用既有固定 runtime，未替换产品依赖：RR/SR 均 310.5，签名 Valid/NVIDIA；
RR SHA256 `50eb21e654a1fd12403e000f5ac9c2488bf9149930c028e73196f3f1d47d10a9`，
SR `22c4660249da5b352cde0ce4d95178c3761223dd6ac096d0b88e5c03d7a5cdd1`；
bridge `1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2`。

## 异常反证与证据边界

隔离 WARP 模块仍通过原 304 项、四项精确运行反证和从独立官方合同读取的
40 个资源 alias 拒绝。另两进程真实移除 WARP device，分别验证冷／录制后
失效 17／20 项；移除两处设备失效检查的副本各编译成功后在指定断言 exit41。
设备失效不能被关闭或 completion/timeout 当成退休证明。没有移除硬件 GPU。

原生 r1 为签名状态字符串预检失败、未编译/GPU；r2/r3 Create 成功但首个
Evaluate 前失败。最终 r4 使用真实 Reconcile、Barrier observer 和冷
SeedCoreRecordingDevice，没有伪写状态表或降低断言。WARP 设备丢失 r3
在同进程重复创建已移除 device 失败，r4 用独立进程运行两种模式。全部
失败保留，不覆盖为通过。

私有回执保留于本地证据根 `rr-b1-20261008`／`rr-b2-20261008`：
`b2-core-r6`、两项 core-negative、`native-rr-r4/validation.json`、
`NATIVE_RR_OUTPUT_ANALYSIS.json`、`B2_NATIVE_CONTROL_FREEZE.json` 和
`warp-negative-r4/validation.json`。包含真实命令/exit、完整源码/对象/
二进制/运行库 SHA 与输出，不提交原始像素和本地绝对路径。

B1 的真实游戏合同仍全部 unknown；以上控制宿主事实单列。主审需判断
是否足以通过隔离 B 门，再允许第 5 节能力 probe／相位 oracle／候选比较。
不得把测试通过、自身建议或无回复算主审批准，也不交维护者中间包测试。
