# Perf11：分离辅助层实例寿命与时域历史 Reset

Related to #277 / #296 / #293，接续已测试 Perf10 #292，基线
`0b01a8e5f6d32cdbc57d7c0c4a7d106a9bdc442a`。这是有证据的辅助层复用修复，
不是宣称已修好所有雨天/FG/CG问题，不自动合并main或部署游戏。

## 新反馈绑定与确切时间线

本次D21new归档只有原始CPU ETL、manifest和同期F8，没有逐帧CSV或D13原始
对照。三者PID均为2592，Core为Perf10 `321c65c4…`；与早先PID22456的录制、
PID23356的F8分开。CPU硬件信息实际记录5800X3D，F8为5080/3440x1440。

ETL覆盖2026-09-19 23:30:58.290至23:31:20.885（UTC+8），共22.594秒；
原录制请求20秒包括WPR边界偏移。F8在23:31:37.316采集，内部同进程日志显示：

| 事件（本地UTC+8） | 证据 |
|---|---|
| 23:30:59.679 | 首次准备从command-state-unverified进入active |
| 23:30:59.682 | 创建主层3440x1440参数 |
| 23:31:01.633 | 辅助实例2创建成功 |
| 23:31:05.304 | 明确F6 enabled=0；NR成功计数停在60 |
| 23:31:12.443 | 明确F6 enabled=1，仍是相同尺寸/模型调参 |
| 23:31:12.592–12.722 | 再次创建1720x720辅助实例3 |
| 23:31:12.730–12.731 | 旧辅助完成精确fence后被释放 |
| 23:31:20.352 | 主层仍是实例1，已连续执行 |
| 23:31:26.479 | 最后F6关闭，晚于整个ETL结束 |
| 23:31:28.682–29.374 | FG释放/创建也在ETL结束后 |

因此本段主要是冷启用→OFF→热启用，最后关闭不在ETL中。不要按口述每6秒
给整段贴OFF/ON/OFF，也不要把最终F8 requested.enabled=0套到整段录制。
这次已经证明是插件F6，不再要求维护者回答同一个开关问题。

F8：`827c5b69aefeb7dd1abf772d76f2b1973eb8d671ece77a2cca537ba607fe7337`。
输入ZIP：`d64ecdfc8e18d0910946b0b1f948ec2fcce4158f2fcabf4f88487c2020c57f33`。
原始附件、私人路径和堆栈不进入仓库，只保留必要摘要和身份。

## 原因与修改

`FindOrCreateContext`原先在辅助层满足同carrier/generation/mode/layer时，
只要parent_instance或parent_input_epoch之一改变，就将整个辅助context退休。
F6使父层历史代次变化，但父实例、尺寸、guide-storage、codec与create tuning
未必改变。结果主层可以用Reset复用，辅助层却重新Create/等待完成/预热。

原本的`AuxHistory::NeedsReset`已经将父历史代次、父实例、辅助代次、尺寸和
父Evaluate连续性纳入历史判断。实例寿命和时域历史不能混为一个条件。

候选只在**所有原有Create合同检查都通过**时，复用同父实例的匹配辅助实例：
更新父历史代次、Invalidate辅助时域记录，并设置`reset_retry_pending=true`。
实际RunOne仍通过NeedsReset→BindNrParameters→DLSSNR.Reset写入重置，只在
成功Evaluate后才清pending并提交历史。拒绝/失败不能吞掉Reset。

父实例/feature generation、尺寸、guide storage、codec/peak、模型调参变化
仍退休，A→B→A不复活旧资源；slot/source.neural或residual-base变化依然由
PrepareOne按原逻辑退休。原Create完成fence、封存描述符、queue/list身份、
资源预算、首尾层预热及异常quarantine没有变化。没有屏蔽游戏camera Reset，
没有采用旧镜头残差，也没有把NGX Create放到后台或在OFF预热。

日志新增一次`[dual-nr] reuse instance=... history=... temporal Reset pending`，
只是选择与Reset请求记录，不冒充GPU完成或有效画面。

生产代码仅修改`src/nr_before_sr.cpp`的上下文选择，性能链其余优化和全部
画质/路线/尺寸不变。生成器更新该文件精确Git blob和新版本，旧guide-history
抽取夹具补齐真实AuxHistory/pending字段，不跳过原验证。

## 聚焦回归

`validate-aux-history-reuse.py`提取冻结Perf10与当前实际上下文选择函数，
搭配生产`AuxHistory`，只替代设备/完成回收服务。旧代码精确复现并返回预期
拒绝码3；候选同历史变更保持同实例且Reset持续有效。

- 连续128个历史代次变化不消耗额外16槽池容量，但每次Reset仍待成功消费。
- 被拒绝的录制不提交新历史；正常连续执行可以复用，跳过辅助Evaluate和
  上游camera Reset仍强制Reset。
- 14类原始存储/寿命合同：父实例、carrier代次、尺寸、Create调参、guide flags、
  codec/peak、不同层/路线、primary不受影响、已退休不可复活、失败latch、
  满表未完成fence拒绝、A→B→A均保留。
- 原guide/context回归和生成器/构建入口测试通过；实际源码Reset前递链、
  成功后消费及预热门控保持不变。CPU替身不等于NVIDIA驱动或游戏实测。

完整Windows/核心GPU回归与成品身份在最终Windows交付记录中单列。

## CPU录制能与不能证明的事

原始无压缩x64内核ETL解析了605缓冲区、6,705,118条事件，其中1,744,919次
CSwitch相邻新旧TID全部连续，无丢事件/缓冲。游戏123,040个执行样本，
119,847个样本键有StackWalk；没有目标TID复用歧义。1ms配置采样率仅作为
名义CPU ms/s，与实际调度驻留时间分开，后者未扣中断驻留。

按F8事件选窗：ON1 23:31:02.200–04.700、OFF 06–11.500、ON2 14–19。
游戏调度约6.95/7.00/5.05逻辑核当量；插件名义self约83.60/125.09/114.40ms/s。
NR运行库执行样本ON1/ON2存在，中间OFF为零，与准确F6时间吻合。

第一段ON的`nvgpucomp64.dll`约2.30核采样当量，后段约0.029；同期
NVDisplay.Container进程各段约0.91–1.00逻辑核。它们是明确的并发工作，
不能假定前后两个ON有相同背景；还不能据此认定驱动故障或全部由本插件引发。
不建议关闭安全保护、杀系统服务或清驱动缓存来凑数据。

无逐帧/呈现/GPU引擎轨迹，不计算FPS、1%Low、FG GPU成本或D13回退幅度。
最终F8主NR/辅NR/合成局部中位时间约7.64/2.95/2.53ms，是历史已完成样本，
不是此ETL每段的同窗GPU计时，不能叠加中位数或换算整游戏帧率。

## 下一次只验证这一处改动

同机同RR后置双NR配置，先第一次开启待稳定，再在同进程F6关→开→关，留足
每段，录后F8。同父实例/同Create合同时，预期辅助实例ID及createCalls不变，
看到reuse/Reset请求，再确认真实成功计数与双层采用恢复。必须保留warmup，
不能以关辅助层或旁路获得的帧率验收。

真正改尺寸/模型/父实例仍应创建新辅助；切镜必要Reset和分阶段预热仍在。
本修复不等于已解决无事件标记的CG阶梯恢复、雨天或FG帧节奏；这些需要含
视频/帧时间的对应证据。持续旁路、无效果、拖影/闪烁或崩溃先回滚并提交
同期F8、Core日志和构建哈希。保持#277/#296开放。
