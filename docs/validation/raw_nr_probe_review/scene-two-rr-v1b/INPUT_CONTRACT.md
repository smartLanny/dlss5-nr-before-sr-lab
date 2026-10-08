# 同源有限3D twoRR 短门输入合同

仅私有 CPU 准备，不改产品，不运行 GPU/NGX。基底是已实际成功的 measure-r2 自有宿主；SceneFrame、scene.hlsl、scene.cso、七平面、camera/projection、采样/灯光/材质均保持原始字节，不接失败的 default renderer observer。

固定唯一入口：`--mode functional --route RR --nr on --warm 2 --frames 1 --out <fresh absolute current directory> --root-exclusive-gpu-go`。其它模式、SR、NR-off、帧数、缺少显式 GO 在 device/bootstrap 之前拒绝。没有 capture/perf/长矩阵扩展。

producer 为原 SceneFrame，R=N=1280×720，O=1920×1080，sample=1，物理 MV=0，sample displacement s 与 NGX jitter j 满足 s=-j。原 reverse hardware depth、world normal、diffuse/specular/roughness、camera=(0,1,-4)、完整矩阵、preExposure=.75/exposureScale2 保持。scene.hlsl 的 raw alpha 固定1，实际旧 functional nr-off/on 共六张读回的全部 alpha u16 唯一 `0x3c00`，由 Astra 独立核实；本包不改输出 alpha 来迁就 flat `.75`。

新 composition 独立固定 alpha1 (`0x3c00`) 域；`.75`、任意其它finite、NaNpayload 等未知 alpha 整像素 CopyResource 底图退回。identity/拒绝仍不写；合法 RGB 编辑保 raw alpha1 的精确 half 往返。与 flat v3 仅 producer alpha 合同不同；ratio-before-shift/正权 bilinear/相位-j/拒绝域/1ULP数值验收不改，不新增clamp、epsilon或保护。

同一个原 native/device/init domain：原 parent Feature13 R→O Quality2/defaultpreset0；独立 child Feature13 R→R DLAA5/defaultpreset0，实际 official optimal 必须再证R→R。child 吃原 same-frame raw+guides/jitter/matrices；NR 吃 clean、color jitter0、一层N=R，沿用同 frame raw depth/MV 明确 approximate，不声称 exact clean visibility。三张clean/edited/composite distinct。原 parent 只替换Color为composite，其余getter合同保持。

录制规模：parentCreate1、childCreate1、child/NR/parentEval各3；五个实际 recording=两次Create+三帧。SceneFrame 初始 producer dispatch 在 parentCreate 内一次，三帧各一次，总计4；childCreate不重复producer dispatch。原七 plane 初始 readback 在同池内由 frame0 后续覆盖，真正保存的每帧七 plane 均在该帧同recording精确退休后读取。每帧所有 packets 共享真实queue/fence/value，完成fence后仍不可变，真实Reset成功才退还，不重Init/不引入epoch账本。

CPU收据必须验证frame0/1生产warmup，frame2实际combine。frame2 oracle预测与实际RGB改变均>0；actual改变只在预测合法域，合法值≤原1FP16ULP，全部alpha逐u16不变，identity/拒绝整RGBA bits不变。低覆盖或全fallback如实停，不自动叠保护。原七平面完整读回验证实际depth/MV/normal/albedo等；实际source frame/token/resource/CSO/constants与输出hash追溯，O RGBfinite/poison及实际runtime/5精确退休。不能预设第3帧会通过，更不能把3帧当画质/时域稳定性验收。

新增接缝仅Flight.Pin保留两个资源槽、NrFlight.Finish接入已测child/composition退休并保留scene_bench::Consume、独立approx资源/child模块、派生自原oneRR的独立twoRR loop及严格新入口/result。原oneRR实现及renderer本体不改。parent/child/NR失败后保留原无retry/quarantine边界。
