# Two-RR approximate v3 固定 alpha 修正窄核

v2 实际录制/34真实退休/finite通过，但所有 composite raw-bit fallback，O全等baseline；错误是shader alpha1与实际固定alpha.75合同错配。新v3只CPU准备，GPU/NGX未运行，原v2/actual不动。

已批准最小域是仅alpha .75(0x3a00)，不允许任意finitealpha。请核V2_TO_V3.diff：实际shader仅一个predicate及注释，native EXE与所有C++/INL/object/其他CSO不变，不重编host；CPU oracle对应改为.75，14数学反例+13源码导出alpha反例通过。unknown alpha1/finite/NaNpayload仍整像素CopyResource底图退回，identity/拒绝不写。SHADER_COMPILE确认新shader-only编译和disassembly包含.75。

请重点核：每观察帧非零oracle预测RGB改变和实际GPU RGB改变；所有实际改变位于oracle合法域；合法域值不扩大原1 FP16 ULP容差；全alpha逐u16不变，所有identity/拒绝完整RGBA位型不变。oracle accepted域与actual changed像素单列，不能将CPU预测数冒称GPU实际写掩码。预测/实际changed-mask可能因原1ULP容差略异，照实记录；未新增掩码UAV或改变算法。

V2_BYTES_V3_PREDICTION_SUMMARY只是固定旧v2 child/NR真实bytes上的CPU新数学预测，不是新v3 actual，也不主张RR跨进程确定性。后16观察帧各预测非零改动；不继承v2新路线的有效编辑PASS。

新GPU规模仅单臂J32(16warm/16observe)、各32 child/NR/parent Eval、34新recording。旧64控制保持原v1 actual/review/EXE身份固定继承。首失败停止，不重试、不叠保护；root串行执行，暂无新GPU。canonical-r3 Core/sourceguard/参数/leases/录制/退休未变，raw guides仍明确approximate。

此目录仅自有源码/窄diff/CPU收据摘要，无vendorSDK、binary、原始日志或真实绝对路径。3D producer alpha1与本已批.75不同，未提前改3D代码或扩域。
