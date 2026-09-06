# F8 单 TXT 与 NBA2K25 Reset 定位（#47 / #127）

## 玩家交付规则

本变更取代“F8 默认 ZIP + TXT”的产品约定。按一次 F8，或点击“生成反馈 TXT（F8，推荐）”，本次交付目录只写一个 UTF-8 TXT；不生成 ZIP、summary 临时摘要或图像。原始游戏日志和旧采集目录不会被删除。写盘过程中有原子写入用的临时文件，正常成功/可处理失败会收尾；进程异常退出不承诺清理成功。

命名规则：`YYMMDD-game-actual-build-HHMMSSmmm-pid-serial.txt`。

例如：`260907-helldivers2-beta0.4.5-fix1-023405006-42-1.txt`。

日期/时分秒采用采集机器当地时间，正文同时保存当地时间和 UTC。游戏取真实 EXE 文件名（去扩展名、转安全小写），版本取构建内真实身份，不把未知版本改成示例中的 beta2。时分秒、进程和序号用于防止连续采集重名；保存不覆盖旧报告。主界面的“复制完整报告”和“复制 TXT 路径”使用本次成功写出的同一文本。

F7 仍是用户主动选择的高级图像补充，保留自己的精确配对核心 ZIP、图像格式、GPU fence 和预算。F8 的文本成功状态不冒充 F7 的 ZIP 配对。F8 新请求仍会清理界面上的过期 F7 路径，不能把两次采集拼成一组。旧 CMD 是独立离线救援入口，不在这里偷偷改变。

## 单文件并不减少关键证据

TXT 从内存汇总同一采集的构建/配置、GPU/驱动、SR/NR/提交计数、运行库模块身份与错误、命令状态和现有 NGX 合同采样，以及经过原有脱敏器的当前/上次进程/ReShade 日志摘要。每个日志段写出实际摘要字节数和 SHA256。未取得、扫描超预算、读期间变化、锁繁忙、取消都明确标记，不复用旧文件或伪造成功。

上限 2 MiB；核心配置和日志只做有界采样，既不请求 GPU 图像读回，也不改游戏或插件设置。`text.complete` 指本次声明的必需核心证据齐全，不表示没有遗漏任何历史事件，更不表示已证明游戏正常。可选证据缺失单独列出。日志给出观察和下一步调查方向，不能保证一次文本能证明所有 bug 的唯一根因或生成正确补丁。

## #127：已证明与尚未证明的区别

用户现有 NBA2K25 / RTX5070 / e5ce121 反馈中，SR 在采集窗口增加115，NR/合成0，运行库未尝试，持续 `awaiting-native-reset`；它不是 NBA2K26 #122 或 NBA2K27 #68。

实际发现并修复的缺陷：完整原生观察已经覆盖同设备/同接口实现时，一张已登记但尚无 shadow 的冷列表，若缺失 ReShade 的前置 reset 通知，原生 Reset 成功回调可能被忽略。本变更在实际成功返回后、游戏继续录制之前，复用已有验证的观察能力并记录该次真实 Reset。失败 Reset、身份不匹配、未覆盖实现和关闭时的冷列表均不借此放行；不新装 Hook，不复制别的列表绑定，不伪造 Reset。

这只是本游戏的相关候选，不是已确认全部根因。另一种可能是新建的列表直接进入首次录制而不调用 Reset。ReShade 的 reset 通知也可能来自创建流程，不能把通知次数当作原生 Reset 证明。本次对 CreateCommandList / CreateCommandList1 加入可选观察凭据，分别记录创建方法、时间、初始 PSO 是否存在、设备生命周期身份，并与真实 init、原生 Reset/失败、销毁、载体调用次数对照。创建凭据本身从不设置 state.known，不开放尚未证明的首次录制链路。

最近12个不同载体样本保留到独立小型历史缓冲，带年龄和丢弃计数；当前快照锁繁忙会输出明确原因，历史不能冒充当前快照。全局计数是独立原子采样，不保证事务一致。可选观察 Hook 对每个实现只尝试一次（每类最多4个），失败保留覆盖缺失，避免每帧/每次创建反复暂停游戏线程。

## 验证与风险

已在 Linux 用 GCC、Clang 执行实际生产代码的命名/预算28项、Reset结果28项、创建观察61项。固定旧 Reset 函数必须精确出现2项失败，新函数通过；编译错误不能冒充负例成功。原 NGX透传38项、受限状态82项、结构与旧 bug 回归继续保留。

真实 Windows 集成测试已补入现有流程：核心收集器实际落盘验证只有一个 TXT、旧结果隔离、取消/繁忙/重复采集；F7 强制显式请求归档；原生 Hook 夹具验证无前置通知的真实 Reset 和创建凭据。未实际执行前不标为通过。没有本轮 NVIDIA 模型、NBA2K25、其他游戏或性能验收。

新增创建观察及历史记录会增加有界 CPU/锁开销；真实共存与帧时间需验收。资源、队列、fence、RR/其他 NR 所有权、格式、shader、jitter、曝光和现有安全旁路保持原规则。NR 无效仍提示反馈，不能为消除提示而放宽准入。

## 本地 agent 与玩家验收

本 PR 不是已发布的新二进制。保持当前版本号，不覆盖同名旧 Release；本地 agent 必须检出本 PR 实际提交，并把 sourceCommit/产物哈希写入交付说明。不要拿 e5ce121 的旧包测试新功能。

维护者按已有 `beta042-validate.yml` / `feedback-integrity.yml` 全量验证，尤其运行：

```powershell
python scripts/validate-core-feedback.py --compiler $compiler
python scripts/validate-f8-controller.py --compiler $compiler
python scripts/validate-f8-controller.py --compiler $compiler --gpu
pwsh scripts/validate-command-state-native.ps1 -ToolchainRoot $toolchain
pwsh scripts/validate-command-state-native.ps1 -ToolchainRoot $toolchain -PoolNegativeControl
```

再运行新增 portable workflow 中的三个测试入口，使用相同固定工具链与完整双语构建。主机 mock、Windows文件IO、WARP、NVIDIA模型、游戏运行分别记录，不伪造云端CI结果。

玩家只需：退出后装入经构建验证的新候选，保留原配置/运行库；进入原NBA2K25场景，F6比较一次；按F8，把一个命名TXT发送到原Issue或粘贴群文档。安全旁路继续存在也要上传这一份新文本。持续黑屏/崩溃就停止、回退，不要求换驱动、运行调试器或反复试崩。

#127 保持开放；#128 的旧两文件清理方案不再作为默认F8交付实现，历史代码和讨论保留。
