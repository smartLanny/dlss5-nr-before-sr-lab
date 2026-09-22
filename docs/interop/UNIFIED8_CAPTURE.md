# unified8：前置 P/N/E 与原生 SR 输出 O

本模块只扩展显式深度采集。旧 `NRStageCapture-v2` 四平面和 NRF v1 不变；旧工具仍可读取原文件。新增 `native-output.json` 与可选 `native-output.nro`，不能用另一次截图、另一帧输出或放大 E 代替 O。

## 真实接点与所有权

`scripts/patch-native-output-capture.py` 在生成 Core 的 `ProcessEvaluate` 中接线：前置实际 Combine 已写 P/N/E 后、原生 Feature1 调用前绑定 Output、源帧、命令记录和活动子区；同次调用成功后，在 SR 计时区间之外复制实际 Output。只处理 `before_sr.active` 且本 slot 的 `stage_capture.pending`，不会把后置/Present 当作前置原生 O。

读取权限取自既有 `nr_source_runtime::SourceStateTable` 的**当前同一记录**传统全资源状态；诊断事件环不作为发 barrier 的权限。分资源、拆分、增强 barrier 或不完整挂钩等不能提供这一证明时，写明确缺失原因。复制前还要求原生命令状态可完整保存/恢复，并暂时取消 predication，再恢复调用后的原状态。

O 使用独立 READBACK，保留原始位、不再应用颜色或增强控件；活动子区原样紧密排列。只接受明确 typed 的单层、单 mip、非 MSAA 2D 存储，当前支持 FP32 RGBA、FP16 RGBA、RGBA8/BGRA8、R10G10B10A2、R11G11B10，最大边长 16384，读回上限 128 MiB。typeless 不猜视图。

资源跟随 P/N/E slot 的原有精确提交围栏、命令记录引用和退休规则。GPU 完成而记录仍可重放时不导出；不得强拆 submitted 资源。原生结果失败、源帧/记录改变、状态未知等只拒绝 O，不能把未提交像素声明 complete。

## 域与回执

O 是原生存储位；格式、Feature1 Create flags、标量曝光均保留，但这些并不足以证明 transfer、primaries 或 scene-linear。因此 `domain=0` 明确表示未验证的原生存储域。HDR 原始诊断存储可以保留，但不能据此做 SDR 残差研究或生成 `scene_linear_verified`。

NRO v1 固定 320 字节头，含 request、NR instance/frame、Feature generation、源 stream/epoch/sequence、命令 lifetime/reset、实际 queue/fence、config/evaluation、活动子区与物理尺寸、格式和来源构建身份；随后是紧密行原始像素。JSON 无论有无 O 都声明 presence/reason；不存在 JSON 的旧文件明确读作 `legacy-capture-without-O`。

`scripts/read-native-output-capture.py <capture-directory>` 同时校验 binary/JSON 和父 metadata 身份、父像素大小、完成凭据、源帧和尺寸。它拒绝跨帧拼接、半写 payload、尚未完成的输出；成功也不宣称跨 P/N/E/O 的光度域已验证。

## 集中验收接口

- 普通 `RequestStageCapture()` 保留无参数接口，配置被持有或性能采样活跃时拒绝。
- `RequestStageCaptureOwned()` 仅供内部已有配置持有者：必须 `nr_f8::g_config_hold=true` 且 `!nr_f8::IsActive()`。不暂时放开全局持有权。
- 使用当次 `g_stage_capture_request_id` 查询 `ReadStageCaptureExportReceipt(request, receipt)`；回执包含 `directory`、`stages_complete`、`native_status/native_reason`。父阶段成功与 O 完整是两个结果。
- 状态 1 可以按原有 request ID 取消；状态 2/3 必须等真实退休/导出，不强行取消 GPU 所有权。状态 0 后回执才可作为该请求最终结果。

深采仍会重建 NR 历史、等待至少 64 次成功评估并在采集后退休。因此性能采样必须先独立完成；这 64 次仅是等待区间，不证明收敛，更不能把深采帧计入性能对比。

## 验证边界

定向验证包含旧格式 CPU 回归、真实生命周期/取消竞态、新文件读取拒绝，以及完整生成 Core 的 WARP 像素宿主。WARP 宿主用构造 P/N 和原生清屏，明确不冒充模型推理；另有独立 NVIDIA 宿主用于真实签名 Feature18 → 原生 Feature1、静态人工场景和人工平面深度/零 MV。真实运行和失败回执保存在私有证据中，完成结果由交付文档记录。

游戏仍需在实际原生 SR 前置路径进行一次深采，确认原生调用的完整状态证据和 O presence。当前人工场景不证明运动序列、RR、FG、未知视图或所有游戏都可读 O；缺失状态是受控拒绝，与 NGX 运行失败分开报告。

## 最终同源复核（2026-09-22）

`5c10a0b7` 完整生成 Core 的 WARP 五场景/564项通过；实际生产辅助预编译宏开启，
字节码与源回执核验。另在同一最终源码、18个正式对象和固定签名310.8模型上，
真实 NR→AMDOF→原生 SR 连续64帧通过，AMDOF recorded=submitted=62，历史延续61次。
第64帧 E′和O的source/request/frame及真实最终围栏对应；NRO与JSON的内嵌源码提交
均为 `5c10a0b72dbe68b0285fe1db0a316db33b065e1c`。宿主1567项检查通过。

E′是此受控宿主另存的实际时域后SR输入，**不是产品旧四平面E的重新定义**。
产品E仍是时域前的合成采样。集中颜色验收会关闭防闪；若手动采集同时启用防闪，
不得把E→O的全部变化归给原生SR，必须先考虑中间的时域处理。当前产品没有额外E′平面。

最后测试仍使用静态人工图像、平面深度/零运动，没有游戏或运动质量结论。
前一次通过宿主漏传编译期源码宏，内嵌身份为unknown；其外部manifest绑定和像素
结果保留，但不计为内嵌身份通过。补宏及二进制/JSON双断言后重跑通过；生产Core本来
已有该宏，未修改生产代码。私有测试夹具修正随后同步公开。
