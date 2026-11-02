# #309 私有 SR：现有材料收口

保留 #338 已证实的 neutral 逐通道接线修复；没有第二个已证实的 shader
修复。受控活动通道漂移仍在，离线 `Z+(private-spatial)` 无净收益，停止
该产品候选。真实游戏成功 private 像素缺失，不能外推普遍坏或游戏通过。
普通恢复继续作为基线，不用另一层历史、盲调 jitter 或扩大算法保护保住实验。

本次只复核现有回执、数字和哈希，没有重跑模型、开图或要求维护者补测。
127 项选定 receipt/帧元数据/冻结源码/EXE/DLL 均匹配；本机完整收口 JSON
SHA256 为 `7ca1ee699ffd6e4ef1ae1cd9d75b3c57d41459942e656e73d3aa47f366fb24e0`。
原始日志、像素和本地路径未提交。

## 精确来源及三类证据

实际 Core 编译源是 `8d89f8e8269b9040aca7d6c55f91a8aad683fff2`，dirty=0；
冻结 PR/docs head 为 `ad37757cc5eed7b4e62bf63e445d28f1d4677837`，不能
改写二进制来源。Core host SHA256
`d6005c9c00de0c6d02f60df8d56ac48915c699d4be85033a2f43dda11a19aa52`。

| 材料 | 身份和范围 | 能证明什么 |
|---|---|---|
| 最终 phase 固定输入 | R=320×192，N=O=640×384；16 暖机+16 观察，source [2,1,18..33]，config2/view1/encoding3/nativeFeatureGen1 | B/Z 在 native Evaluate 后注入；guide nearest oracle=0，actual Bind jitter0/MV1/Reset0、history/sourceGap0；native输出相位未证明 |
| 生产连续 capture | R=N=320×192，O=640×384；1层 source [2,1,3..18]，3层 [2,1,5..20]，各16有效帧/160平面 | 实际 native/private Feature1、Feature18 和 Flight/worker 合同；首有效帧 BindReset=true(causes7/history9)，不能冒充固定暖机实验 |
| 四份真实游戏 F8 | 实际 Beta15 Core SHA4116…/source8d89；前三 Before R=N=2228×1253，第四 After N=O=4K | 私有采用增量均0；unsupported route 或768MiB cap拒绝，没有成功private像素，不能组成普通/私有质量或成本A/B |

生产 capture 三个控制 case 合6058断言；关闭 case 私有 Create/Eval=0，
无 capture worker/readback jobs。这是此前受控 synthetic 生产验证，
不是本次新增测试或真实游戏画质验收。

## 第一变化和已证实修复

legacy/new 50%阈值对照，16对应帧的 B/Z、carrier/tail、depth/MV、private
reconstruction 和匹配空间参考逐哈希相同；旧 inactive 最大增量
0.0009765625、最多20448样本改变，逐通道修复后16帧的 inactive 增量及
改变样本全部为0。99%/actual61阈值回执亦为0：这是 neutral 接线因果证据。

活动通道固定输入仍漂移：最终8d89 phase 的 B/Z/carrier/tail 各1hash，
private/final 各16hash；zero/matched 两组对应 private/final 16/16相同。
source [2,1,19]→[2,1,20]、像素(374,304)R：B=0.02000427246、
Z=0.03201293945、carrier=0.505859375、tail=0.0002899179235 均不变；
private 从0.50390625变到0.50537109375，final 从0.02810668945变到
0.03103637695；实际 Bind Reset/history/sourceGap 均0。较大时间变化
最先出现在保存的 private reconstruction；模型内部原因仍未知。

空间 O→N→O 更早已有损失，和新增时间漂移分开。固定50%编码往返最大
3.7253e-9，不能把全部问题归给 tail 精度。离线 corrected 候选的 RGB
平均 MAE：50%空间0.000258802、corrected0.001637409；actual61空间
0.000244252、corrected0.001933505。受控对照未见净收益，不进入产品，
该指标也不裁决所有游戏的主观画质。

## 精确缺项与保留范围

- 真实游戏的同源 B/Z、E_N/carrier/tail/support、原始和归一化导引、private
  raw/compose、source/epoch/sequence/config/recording，以及匹配 GPU 空间参考。
- 原生输出相位，存在时原生 exposure 纹理的像素及元数据；constant1不能
  替代真实场景HDR曝光。新RR合同不能借 privateSR 的 jitter0/synthetic 过门。
- 实际游戏成功private采用和显示证明，以及同N、排除capture/F8窗口的整链
  CPU/GPU关键路径、帧节奏和VRAM；历史混合config/instance计时不可用。

没有新因果证据，不再改 privateSR shader，不重复55/77/69FPS三组或已测
快捷键。若后续判别确实需新材料，只复用现有 Flight/worker 补缺的同源
GPU空间参考/原生曝光，采集失败写 missing，不改变 NR 准入，不发布独立
诊断包。#339 和新 RR 的生命周期/接入通过不能替代上述画质证明。
