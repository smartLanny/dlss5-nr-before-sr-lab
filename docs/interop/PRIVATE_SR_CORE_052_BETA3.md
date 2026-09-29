# 0.5.2 Beta 3：私有 residual SR 产品接入候选

本候选把独立私有 Feature1 接到 Core 的原生 SR 后置最终修改量上。源码接入与
游戏验收分开登记：完整统一构建、CPU、WARP、显卡及实际Core受控真实Feature1
验证已通过；游戏操作暂停，游戏画质、稳定性和性能仍待测。
`PrivateResidualSR=0` 默认关闭；已有设置和旧候选继续保留。

## 数据与执行边界

- **B** 是本次成功原生 Feature1 输出的不可变 FP16 快照，**Z** 是同一来源、
  同一录制中整条 NR 链完成后的最终受控结果。私有 SR 接点位于多层运行及最终
  combine 之后，全链只处理一次 `Z−B`，不在每层重复私有 SR，也不丢第一层贡献。
- 修改量经面积降采样到 N、有界 FP16 载体及 FP32 尾差编码，由独立 Feature1
  重建后加回 B。成功录制合成之前保留 Z；首次创建或等待创建完成不能算已采用。
- 导引在**原生读取边界、opaque native Evaluate 之前**录制：保留真实source、
  recording、原有效rect、原始MV.Scale、原始jitter及feature flags；生成零原点
  N尺寸的R32_FLOAT深度与R32G32_FLOAT运动向量。空间采样使用有效区域；运动单位
  为原始MV.Scale乘以N/R，不能使用已被NR工作比例改写的scale或贴图分配尺寸。
- `Capture/Record`只确认已录制导引。原生成功、B快照与最终Z完成后，`Finish`
  才核对同源、同录制并确认post-SR相位。native失败时guide-only工作仍有自己的
  提交和退休责任，不能因为没有消费导引就提前释放。

## 首批支持范围与拒绝条件

首批限定D3D12 Direct队列、单节点设备、原生Feature1后置、已证明FP16 SDR域、
无FG拥有的晚期路线、非jittered-MV，且**原始输入jitter已知并严格为零**。
未知jitter与非零jitter分别报告`JitterUnknown`、`NonzeroJitter`。常规带jitter的
游戏帧尚不在这批可用范围；不得修改游戏jitter或伪造known来绕过准入。

NVIDIA NGX SDK固定快照`a291cc7`的`include/nvsdk_ngx_defs.h`第292–293行分别
定义`MVLowRes`、`MVJittered`，第733–734、762–763行定义MV.Scale与Jitter.Offset
参数。**MVJittered与输入jitter是不同证据**：向量不含jitter，不等于深度/MV纹理
采样位置已与post-SR输出对齐。当前normalizer没有非零jitter相位重采样；这是本候选
的保守限制，不是声称SDK要求所有后置方案必须使用零jitter。

未知source、录制身份、运动单位或采样相位不会被补成有效值；不匹配的有效区域、
HDR、原生RR、占位/未支持的线性深度及相互矛盾的深度方向均拒绝。状态检查另有
一个严格限定的入口例外：已观察到的D3D12输入状态必须支持compute读取且无写状态
矛盾；**未观察到精确state时，仅在真实原生读取边界**利用其shader-readable合同
直接采样，不插入猜测的输入transition。此例外不能扩展到任意post hook或Present。

已有残差防闪与私有SR同时被请求时，本候选拒绝私有SR并报告冲突；不自动关闭防闪、
更改层数、比例、模型或任何已保存参数。独立关闭私有SR后不再录制新的私有导引或
Feature1工作；已录制工作的准确退休继续进行。

## 生命周期接线

生成的Core由`RecordNrUseLocked`登记依赖后执行导引Capture，并在调用原生SR前
恢复入口命令绑定。私有Finish仅在成功原生结果、成功B快照及最终NR合成之后进入。

`ProcessSubmittedCommandLists`在队列锁下执行Capture batch → 原生Execute →
同队列私有Signal；只有成功门中的`CompleteSubmissionBatchLocked`交付私有
`Submitted`回执。ReShade pre-submit通知、Record次序、帧龄与post-boundary CPU
序号均不替代提交证明。前序录制尚未提交时不推进下一次私有历史。

成功native Reset或Destroy结束相应录制寿命；失败Reset保留引用。每个flight必须
同时满足**自己的准确fence已完成**与**该录制不能再重放**才可复用/释放。未提交
丢弃、提交捕获失败、Signal失败、身份/队列矛盾等走隔离，保留不确定依赖。
关闭、配置变化及上下文退休通过Collect/Ready/Destroy收尾；`Cancel`本身不是释放
证明。公开内存计数仅涵盖已计入的plane，不含opaque NGX内部内存。

## F8怎样阅读

先看`requested.privateResidualSR`和`requested.privateSR.configurationGate`。
后者是当前配置门，可能为off、master-off、防闪冲突、路线不支持或等待Feature1；
通过这些配置门仍不代表通过导引和运行时准入。

`*.privateSR.lastEvent`记录最近事件，可能属于关开开关前的历史；它不是当前运行
状态的独立证明。结合`guideReason`及同一配置窗口中`guideCaptures`、`recorded`、
`composed`、`submitted`、`adopted`的增量判断。录制、提交和采用计数不证明最终
屏幕正确；持续拒绝时保存F8及同次日志，不将旁路后的高FPS记作优化收益。

## 身份与验证登记

| 项目 | 最终本地候选 |
|---|---|
| 编译及包装源码 | `4f5ce993c6341e4649496cc618d105ef77a7d538`；后续测试/文档提交不改变此身份 |
| 候选身份 | 0.5.2 Beta 3；不重标旧Beta1/Beta2或独立实验构建 |
| 完整统一构建 | `build-unified-candidate.ps1`退出0；全人脸后端、双语编译/静态分析、全部累计门、WARP、恢复反例和最终重建通过；日志时间跨度1239.32秒 |
| 本轮导引验证 | CPU39项、WARP96项显式检查及深度像素断言通过；另核对最终构建源闭包 |
| 实际Core受控API | WARP/RTX 5090D各11组：首创旁路、零/正负/微量/累计修改、25/50/100%及奇数尺寸与非零区域、guide-only、失败Reset、丢弃、重复提交、先Reset后fence、关闭退休 |
| 实际Core提交补测 | WARP/显卡各补2组：双录制反序提交与Signal失败；同一最终Core，补测fixture身份单独保留 |
| 实际Core真实Feature1 | 1次私有Create、5次私有Evaluate、6次提交、1次退休；同帧真实原生B与零修改输出逐字节一致 |
| 游戏、动态画质、游戏性能 | 未验收；没有启动游戏，也没有本候选真实Feature18多层推理通过结论 |
| Debug layer / 托管CI | 本机缺D3D12 debug layer，仅GPU回读通过；托管CI未运行，均不写成通过 |

完整生成Core的SHA256为
`0e81ca1c5cd607c324a19843ab6da9528c469dfe95bb366357202943f89f62aa`。
可公开的精确身份、脱敏命令和回执摘要见
[PRIVATE_SR_CORE_052_BETA3_RECEIPT.json](PRIVATE_SR_CORE_052_BETA3_RECEIPT.json)。
原始命令、各门JSON、日志和测试图保留在本地，不将dump、厂商DLL或玩家反馈入库。

| 文件 | SHA256 |
|---|---|
| 中文Core | `a3909d22a508da5e3adfb897409dbc082985d417232adac27c75c0917a9fa3eb` |
| 英文Core | `ccc035263b822fd445aa0b82cac7673936fc46b2be38cb8cc575d63952b081b8` |
| 中文ZIP | `6c0219d06f0c8a015e3ebb1f0274a5547fcf0e631523b146762a42b0949138ba` |
| 英文ZIP | `c26e22a092082908f5c57b27c43cbafc53f537c01f38636128d3173cabee5f11` |
| 构建及宿主验证使用的nrchain | `1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2` |
| 宿主SR运行库310.5.0.0 | `22c4660249da5b352cde0ce4d95178c3761223dd6ac096d0b88e5c03d7a5cdd1` |

本地ZIP仅包含单语言Core及通用说明；私有SR短手测与本说明随包放置，未发布OTA。
nrchain和SR DLL的哈希记录验证依赖身份，不表示ZIP携带或需要替换这些组件。

### 验证分层与测量边界

受控API测试编译**未修改的最终生成Core**并链接经回执核验的实际对象，使用真实
`Capture/Finish/ProcessSubmittedCommandLists/ProcessCommandListReset`等回调。
它手工安装真实Owner，模型结果由fixture提供；零/正负/微量及多个贡献相加的总Z
有整图像素断言，但不等于真实Feature18多层推理。静态核验确认唯一私有Finish
位于实际`nr_dual_runtime::Run`、最终combine及UAV屏障之后。

提交补测确认第二个尚无顺序证明的录制先被Core资源租约门拒绝；独立登记其真实
token后，私有Capture自身也拒绝Busy。反序提交后第二Z整图保留，未推进私有历史，
第一录制按自己的fence与Reset退休。Signal失败仅在宿主临时注入`E_FAIL`，原Core
提交路径隔离依赖、零采用且不提前释放；这不是驱动实际故障复现。

真实Feature1宿主进一步验证实际CreateOwner/原NGX出口、原生读取前Capture、
成功原生SR产生B、私有Finish、成功提交与准确Reset退休；没有替换Core或私有API。
宿主仅初始化一次NGX；Core复用其原始接口。关闭、防闪冲突、RR/HDR、非零jitter、
错误运动尺度、无效区域、缺少source均验证拒绝且不创建私有Owner，防闪设置保持。
Release钩子旁路另通过108个实际提取钩子用例；未声称已注入所有Create/Evaluate递归情形。

真实Feature1测量使用RTX 5090D、驱动32.0.16.1692，R=N=320×192、O=640×384，
6个静态零修改帧、零jitter、平面深度及零MV。只衡量受控宿主，不外推游戏：

| 指标 | 实测 |
|---|---|
| 首帧CPU录制 / 提交 | 42.1125 / 1.9372 ms；录制包含原生SR、拷贝和私有创建 |
| 后5帧CPU录制 / 提交 | 0.1814–0.3908 / 0.0385–0.0717 ms |
| GPU导引 | 首帧0.005792 ms；后续0.003520–0.003616 ms |
| GPU私有段 | 首帧创建0.032896 ms；后5帧Evaluate/合成0.109984–0.131744 ms |
| 自有plane峰值 | 12,779,520字节（12.1875 MiB）；退休后此计数为0 |
| DXGI本地进程占用 | 前78,553,088 / 执行中256,405,504 / 后240,480,256字节 |

DXGI值包含宿主纹理、回读及NGX缓存，不等于自有plane计数；没有宣称opaque NGX
内存全部归零。准确fence完成但录制仍可重放时不得退休，Reset先到而fence未完成时
同样不得退休。上述计时不能证明降噪算法3%开销目标。

失败记录保留：早期统一构建在旧Release隔离测试缺少新增私有旁路声明时停止，
修复测试适配并保留历史泄漏反例后完整重跑通过；真实宿主早期回读错误地给只读B
使用UAV状态，已改正宿主读取方式并重跑。未绕过失败门，也未用旧实验结果填充本批。

独立实验的受控Feature1记录及历史边界保留在
[POST_PRIVATE_SR.md](../../experiments/nr-edit/POST_PRIVATE_SR.md)，导引范围见
[PRIVATE_SR_GUIDE_PRODUCER.md](PRIVATE_SR_GUIDE_PRODUCER.md)。旧实验成功不证明
本候选的游戏接线、动态稳定性或性能。本功能也没有给出“新增GPU时间≤3%”的新降噪
目标达成结论；该目标需要独立同工作量测量和画质验证。

## 以后由用户执行的短手测

当前不启动游戏；由维护者之后统一安排：

1. 保留旧包和配置，固定一个原本可用的原生SR场景，记录Core哈希、驱动、R/N/O、
   层数、HDR/FG及实际路线。先保持私有SR关闭，保存基准F8与画面。
2. 单独开启私有SR，读取configurationGate、guideReason及同窗口计数。若因非零
   jitter等拒绝，照实登记未覆盖；不为了通过而修改来源证据或游戏jitter。
3. 只有实际准入与提交成立后，比较固定机位及移动→停镜→遮挡的短片，检查有效NR
   细节、文字边缘、闪烁、拖影和颜色；多层仍须确认最终只处理一次修改量。
4. 单独关闭后确认无新增私有GPU录制，随后观察既有资源退休；再开启检查重建与
   恢复。防闪冲突单列记录，确认双方保存设置未被自动改写。
5. 出现持续异常即保全第一次F8/同次日志并停止该轮。正常退出、未测组合和性能
   测量分别登记；不以成功计数替代画面，也不把采集开销算作性能结果。
