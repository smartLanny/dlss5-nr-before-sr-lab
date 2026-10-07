# 两网格真实采集与 clean-guide 边界

2026-10-08；#309 / draft #342。此报告记录开发侧输入证据，不是产品新 RR、
Feature18、画质或游戏验收。原四历史 SceneRR 相位结果见
[固定相位报告](RR_GPU_MATH_AND_PHASE_PLAN_20261008.md)。

## 一次执行的采集结果

同一几何、当前/前帧变换与 owner，分别以 raw Pj 与 clean P0 真正 raster/depth
test；不从 raw 插值生成 clean 附件。场景含斜面背景、移动近矩形、遮挡/显露、
非恒定 depth 与不同材料/实例/primitive IDs。源与输入在硬件运行前冻结。

指定帧 16/17/18/19/20/21/22/24/25/30 各独立进程只运行一次，共20个
1280×720 grid，全部 exit0；无 NGX、Core、性能测量、重试或参数调整。

- 73,728,000 个 same-PS32F→native16F component bits 与独立 RTZ 转换一致。
- 18,432,000 个 D32 readback bits 与同 fragment SV_Position.z 一致。
- 上传 vertex/constant bytes、visible instance 的 normal/roughness、ID owner、
  附件长度与冻结格式/extent 的注册精确门通过。
- 259 份证据文件封存一致，含180份完整原始附件；源65文件保持不变。

有限 ray/raster 诊断的3360点 instance 一致；world/MV/depth 残差保留原数值，
没有验收阈值，因此几何精度仍 **unknown**。实际上传 P0 的 near 是 float32
0.10000000149011612；仅离线纠正旧 nominal-double near 的诊断，不改原输入。
debug layer 实际不可用。冻结 EXE 未输出 fence completed 数字或资源 GetDesc
指针：完成性只依据成功执行的 Finish 源合同，不补造实际 trace。

源 manifest SHA256：
`660e8aae77ef2df0b89f414a73112a530849de6a5ea990592dac6aee5793b616`。
采集 manifest SHA256：
`494b0f7e09a7414306d74cdff094d37307ff9b7c2655c14a5172417ad63f7441`。
精确门 receipt SHA256：
`5fe5cccdd7eac7c369e8e10421e21e89912ce7336220cd287cca43c00b34f08e`。

## 不能由 raw guides 唯一恢复 clean visibility

已固定一个精确有理数反例：s=(1/4,0)，raw ray x=n+.75；在 x=100.5±1/16、
y∈[100,101] 放置无阴影的不透明薄片。所有 raw rays 不见该薄片，clean ray
(100.5,100.5) 可见。两个不同场景的所有 raw RGB/guides 相同，clean visibility
却不同。此反例不依赖误差容限或模型行为。

因此，宿主能重新渲染 P0 guides 不证明当前游戏能提供它们。当前游戏没有几何/
surface-ID/P0重渲染 producer，fractional raw depth/MV/normal 的 nearest、
bilinear 或“小 depth 差”都不能成为 verified clean 输入。下游 edit mask 也不能
修复已经送进不透明 NR 的无效 guides。

两 RR 链在 NR(clean) 前需要真实 clean producer，或严格证明同时间、语义、
extent/subrect/identity 全 known 的 coincident s=0/整数 copy/gather。缺少这个
输入合同时整条新链拒绝，保留已支持的普通 native RR / RR→NR 路线。

## 独立的一次 RR 可行性

维护者另外授权的低分 NR→一次 native RR，可先在隔离宿主固定 NR N=raw R，
复用 raw 同源 guides，避免另造 clean guides。这不是两 RR 的改名或替代结论。
它仍须实测 decoded NR 与最终 editedRaw 保持 raw 的 c=s 相位、曝光往返和一次
native RR 的采用/退休；NR 输入同源不证明 NR 输出同相位。

当前仅批准独立宿主 CPU 准备；未修改生产 PrepareBeforeSr 的 Feature1-only 门、
RR Before 拒绝或恢复旧私有 Feature13。没有可交游戏的该路线功能。
