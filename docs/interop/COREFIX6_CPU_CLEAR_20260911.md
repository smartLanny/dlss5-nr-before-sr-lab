# Corefix.5 复测与独立 CPU Clear 格式请求

## 复测事实

两份维护者反馈的 Core 身份均为 `0.4.7beta-corefix.5`，运行源为
`f2dc19f56d07bfe7c6d1effa271ea321cbf391d6`。原始反馈和逐字段分析留在私有证据中。

死亡搁浅仍无持续可见效果，偶尔闪烁。选定采样帧的 2040 个坐标全部写入并完成合法存储区间比较，
无缺样或不匹配。这只确认该采样点的真实 Output 写回，不能代表普通帧或最终显示。
同一采集窗口的 NR 和提交继续增长；已采到的近期帧没有 reset reason。
后续绘制/清除的部分目标尚未识别，不能直接认定它们覆盖了 Output，也不能按 CPU 回调顺序
替代同次提交的命令列表数组顺序。本次不声称已定位其闪烁根因。

绝区零先有 NR，切换后在新的 Feature/config 范围内持续拒绝 typeless Output。
当前原生 Clear 的非 shader-visible CPU UAV 描述已知，而 GPU 描述符槽已淘汰；
旧范围的成功写回样本不能证明当前范围通过。Corefix.5 的独立 CPU 请求记录仍要求完整
CPU/GPU pair，导致 GPU 缓存缺项连同可观察的 CPU 格式声明一起被拒绝。

## 外部源码核对

- [OptiScaler NR 固定版本](https://github.com/Dagherbou/OptiScaler_DLSSNR/tree/973761621353b99bee3dc7d4bb27b117fef2644f)
  同样在 SR 后运行 NR，并将自有模型结果写回原 Output。其 `OutputResourceBarrier`
  来自自身配置/适配层，原生 DLSS 默认也假定 UAV；它不是 NGX 参数自动提供的真实状态，
  因此不能直接作为本次状态修复。没有把观察器缺少 barrier 事件等同于违反 NGX 合同。
- [原神集成项目固定版本](https://github.com/CXP-2024/dlss5_for_genshinimpact/tree/d8117ae530cab690da7874a97989e2e2201cabd1)
  的活动 NR 核心来自外部二进制，公开内容主要是集成、配置和 D3D11 到私有 D3D12 的桥接补丁。
  它记录过私有队列观察不全导致新版插件反复拒绝，并改用旧版插件；这是项目自述，
  不作为当前 D3D12 Core 故障的实测证明。FP16 carrier 和回填机制不能替代当前资源的格式合同。

本次只参考机制和证据边界，未复制这些仓库的生产实现，也未执行其发行物。

## 修改边界

[Microsoft ClearUAV 合同](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearunorderedaccessviewfloat)
明确区分当前 shader-visible GPU 描述符、non-shader-visible CPU 描述符及显式资源参数。
稳定的 CPU 描述符可说明本次 CPU Clear 请求的 typed format；它不证明 GPU 描述符内容或执行结果。

候选只解除“GPU 槽未观察到/已淘汰”对独立 CPU 格式声明的错误依赖。
设备、资源、地址、形状、CPU 描述符前后版本、原生成功以及当前调用/Feature/config 的核验仍保留；
已知 GPU 冲突、错误 heap/device、退休、写入中和真实观察覆盖缺口仍拒绝。
准备阶段的缓存声明还须经本次原生调用再次核验，才允许插件使用自有 typed view。
GPU native-use、提交验证和最终显示证据继续独立，不因 CPU 格式候选可用而升级。

## 验证状态

实现与定向验证进行中。尚无 Corefix.6 的游戏、NVIDIA Feature18 或性能通过结论。
冻结源码、实际测试结果和产物身份将在完成后补入本节。
