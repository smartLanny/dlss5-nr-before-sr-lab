# #309 B1/B2：原生 RR 输入 identity 接入

本批是新 NR→RR 路线的接入前置，尚未接入产品 `ProcessRrPost`，没有
私有 Feature13、NR、编辑传输、第二历史、global Init 或新 UI/INI 开关。
默认原生 RR→后置 NR 和 Before 拒绝条件保留。不能将它称为已经完成
Denoise-first、游戏消闪或性能修复。

## 身份与合同

任务基线为 main `024f9cc32778c1056db364d76d087ee257da6502`。独立审计
使用 #339 已完成的 `f9ef953faad0a31d280d27adbbe411433a352a47`；其
ProcessRrPost/source/binding/guide/projection 路径与基线相同，生命周期
相关文件差异在 JSON 中分别保留。不能把二者的完整源码哈希混同。

[机器合同](../../docs/validation/RR_B1_STATIC_CONTRACT_20261008.json) 列 53
个角色与 137 个官方键/子区域键检查、所需/可选分类、物理格式/网格、
采样相位/单位、曝光、透明/材质/反射、源帧与完成证明字段。所有运行时
`actual` 均未采集，`runtime_verified=false`；同名 pointer 不是验证。
官方依据为固定 NVIDIA Streamline v2.14.1 RR guide/NGX headers，未替换
产品 SDK。详细审计原件留本机，提交版剔除本地绝对路径。

## 隔离实现

`src/nr_rr_identity_dx12.hpp/.cpp` 只负责原格式、全物理 Texture2D 副本
和一次原生 thunk 的参数 scope。首版只接受单 mip/array/sample、明确
R/O/非零子区域、typed FP16/FP32 和已知 shader-read 状态，完整复制保留
所有 texel/alpha，包括 active rect 外。曝光值只校验并原样转交，不归一化，
不套用 residual Feature1 的固定曝光 1。

LeaseAdapter 借用调用方真实 recording/current/resource lease，模块没有
Execute/Signal/fence 账本。生产适配必须使用既有 NrSlot、RecordNrUseLocked、
CanMutateSlotLocked 及 NativeCommandStateScope，不能用实验布尔替身放行。
原 Color 和 copy 的 lease 在首条 GPU 命令前发布；native-consumer 标记在
opaque 调用前发布。未知 native 消费不能使用 copy-only discard 规则。
副本经过 native 后没有状态证明时，安全退休后换新副本，不猜 barrier 前态。

仅修改已有的 Color 表示，保存 typed/void getter 的返回码和值，写后及
恢复后验证。其他 NGX 参数、subrect、jitter、MV、guide/曝光不改。已知
semantic 别名或 ColorBefore/After/透明层关系不支持时拒绝；静态键检查
不能替代调用方完整 B1 证明。普通拒绝/可恢复准备失败调用原生恰好一次；
无法恢复参数、已知设备丢失或 opaque 故障最多一次，可能为零，不重跑。
关闭实验不查询/分配/录制 GPU 工作，也不能绕过已有 quarantine。

## 已运行的验证

`tools/validate_rr_identity.py --compiler <pinned-compiler> --output <evidence>`
使用本机固定 llvm-mingw，实际编译模块与 `tests/rr_identity_gpu.cpp`。

- 正向 WARP：304 断言，FP16/FP32 全物理字节、非零子区域外和 alpha 精确
  一致；同 Context 同尺寸 FP16→FP32→FP16 切换、缺键不插入、参数两表示
  恢复、一次原生调用、不同 queue/token/module/feature、未知合同、预算和
  envelope/lease 拒绝均覆盖。
- 参数写入不生效、typed/void 分离、恢复失败、native 失败、代际失效、
  envelope 恢复失败和 consumer 标记失败分别验证；失败不重跑，关闭不能
  逃过 quarantine。录制未提交、仅 fence 完成或仅 recording 释放不能退休。
- 四个生成副本各编译成功后，移除 CopyResource、移除 Color 恢复、增加
  第二 native 调用、移除关闭时 quarantine 门，均在指定断言精确 exit 41。
  正向 exit 0；没有修改正在验证的生产文件。

这里的 native callback 是受控 CopyResource 消费者，envelope/lease 是明确
标记的隔离宿主 adapter；这些结果不算 NVIDIA NGX 或生产 adapter 通过。
真实 Core command-state/lease 适配验证另行记录，不能借用这 304 项充数。

## 后续审查门

按 [#309 执行单](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/309#issuecomment-6034217346)
提交 B1 合同、B2 关键 diff、真实 adapter 正负控与调用计数供主审。运行时
完整 RR 材质/反射/透明/曝光合同、同输入/初始化/预热的原生重复基线仍缺失。
在审查通过前不接产品，不执行额外私有 Feature13，不选择 ratio 分母、
firefly 阈值或 jitter 符号，不让维护者分批测试该宿主。

最终方案 `PrivateSceneRR(R→R) → NR(clean R) → rephase edit → raw' →
native RR(R→O)` 是两次 RR，须独立证明 factory 域、实际创建/推理能力、
三份相位、HDR/exposure、资源/历史隔离和成本。它与低分 NR→一次原生 RR
的可行性问题分开记录，当前均不冒称已经可用。
