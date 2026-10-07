# SceneRR 开发侧数学门与模型前相位计划

本页补充 #309 / draft #342 的隔离验证，不新增产品入口。产品仍为 RR→NR，
新路线目标是 SceneRR(R→R)→NR(clean R)→移回 raw→native RR(R→O)，共两次 RR。
OptiScaler 对照见 [固定参考](OPTISCALER_DENOISE_FIRST_REFERENCE_20261008.md)。

## 真实 GPU 数学门

WARP 和 RTX 5090 D 分别完成 **655 checks**，十个真实 shader 负控均精确
exit41。323 份私有源/命令/二进制/输出/回执由 manifest 固定；没有调用 NGX。

分别检查三个不同对象，不能把理想计算说成实际 raster 逐位一致：

1. 正权双线性 tap/weights、编辑传输 P/S/D、明确拒绝域及精确手算值。
2. 实际投影→viewport→插值产生的 q；独立 CPU 消费实测 FP32 q，核其算术。
3. 理想坐标与实测坐标残差、固定竞争假设的判别范围；不 snap 实测 q。

保留两次原始失败：透视 varying 不能当理想 pixel 坐标，固定单平面改用
`noperspective` 后仍保存真实插值误差；CPU half 的 RNE 不能冒充 RT store。
按 [Microsoft §3.2.2](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#3.2.2)
核 FP32→FP16 render target 的 RTZ，同 PS 的 FP32 附件供独立核验。
FLOAT typed UAV 的 NaN payload 也不能假定逐位保存；原始位型退回仍需
copy/不写或明确整数路径证明，不以普通 float 写回代替。

这些检查不证明模型相位、一般 guide 合同、画质或性能。

## 模型前固定的四历史实验

完整实际 Core 来源 `0660260f52e84eb729f70a83325646f5e11df9a0`，337 项源 manifest、
编译 rsp 与复用对象重新核验。新私有宿主保持已有 SceneFactory/Flight/opaque
恢复、immutable packet、精确 queue/fence 与真实 Reset 后退休，生产源码未改。

- Z/Z2、J/J2 各独立进程/parent/child/history，每组16 warm+16 observe。
  重复组读取相同输入 bytes；32 个每帧 MV/specMV 资源独立，不覆写 pending 输入。
- 两份 Z/J 共64帧真实 D3D12 raster，固定低频三波、棋盘/细线和三块 HDR patch。
  实际 VS 平移物理 m；场为 `F(u+s-m)`，MV=`m_previous-m_current`，无 jitter 差。
  Pj 仅用于实际采样，NGX 收独立 P0 与 `j=-s`；依据固定官方 Sample/NRI 直接链，
  不外推所有游戏。16观察帧分静止、移动、停止三个区段。
- preExposure=.75、exposureScale=2，raw RGB 仅在 PS 乘一次.375。HDR patch
  输入实际 FP32/16F 为 .09375/.375/1.5，只报告该固定曝光的输出增益/偏置/漂移。
- 常深不透明单平面、恒定材料与重合 specMV 是明确受限输入；不证明非恒定深度、
  真实反射、遮挡/显露或以后 NR(clean) 的一般 guide 对应关系。

## 判读预注册与反证

固定 ROI、三个谐波及位置假设 c∈{0,+s,-s}。U 仅为三项：所有观察帧最坏配对
重复角差 + 真实 raw 拟合相对解析相位最坏误差 + 当前输出 binary16 一 ULP
经固定线性拟合的传播界。重复差是经验量，不是普遍模型误差界或置信区间。

模型执行前，用真实 raw 误差与实际 FP16 量化证明原 v1 交叉频率双区间门连
理想输入也全部 unknown。原 helper/plan/driver/seal 保留；新 v2 明示采用有限
假设兼容性：唯一候选所有波 r≤U，每个竞争者至少一波 r>U。只测量角具有 U，
固定预测点不再重复给第二个 U。它不同于 v1 额外双区间稳健门，不称等价重写。
没有缩 U、改输入或按模型结果调 ROI。

9 个 actual-format 正控（3种 s × 3种 c）识别正确；重叠、前帧位置、频率
不一致及两重复组共同偏移/滞后反例正确拒绝。Z/Z2 所有观察帧和 J/J2 的
s=0 帧须先通过固定当前位置零基线；不事后减去共同偏差。所有 informative
观察帧、三个运动区段须同结论，否则 unknown，不自动选最低残差。

Astra high 已核宿主/输入和完整 v2 聚合方向 PASS。四历史实际模型执行单独
记录，不能把本页的 CPU/GPU 数学结果当 NGX 相位通过。本页不含模型输出结论。
没有游戏、运行库替换、候选部署、公开包或最终 canonical 通过。
