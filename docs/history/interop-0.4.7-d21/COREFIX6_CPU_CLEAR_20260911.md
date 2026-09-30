# 0.4.7beta-corefix.6 源码交接与 CPU Clear 格式请求

## 冻结身份与产物

Corefix.6 冻结源为 `04bb64fa64b8020a21f8283f5d99cfc581cdbc12`，来源清单 SHA-256 为
`8372455b9c3e092f1973c4ef8bff453d81f82312994b917e283ec6364f77194d`。
清单版本为 `0.4.7beta-corefix.6`，Core PE 版本为 `0.4.7.11`。

最终同源验收 `evidence/corefix5-retest-20260911/build-r4/validation.json` 成功通过 78 项检查，源码清单、双语编译、CPU 与 WARP 均通过。产物 SHA-256：

| 产物 | SHA-256 |
| --- | --- |
| 英文 Core | `6913c0294a0b2f09dc30f3e70df1a75749dddf29298817f5a8a9df9be3aea21d` |
| 中文 Core | `d1a0ddc02884e06309fe9b46f45e08085db39aaf133ef5f1c876ca7084539ce9` |
| `nrchain_nvngx.dll` | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

本轮没有复制外部源码或 payload，也没有修改 vendor loader。外部项目只用于机制和证据边界对照，不能替代当前 D3D12 合同或真实游戏验收。

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

## 修复合同

稳定的 non-shader-visible CPU UAV receipt，在显式 `pResource`、设备、资源 lifetime、地址、形状、typed non-typeless 格式、原生成功和当前调用身份都精确匹配时，可以保留独立的 CPU typed preparation 声明。

GPU receipt 仅在槽位未观察到或已被容量淘汰时可以缺失；当前绑定的 shader-visible heap、heap lifetime 和设备仍须可核对。已知 GPU view 冲突、错误 heap/device、资源退休、写入中、覆盖缺口和 CPU 前后版本变化继续拒绝。两侧 GPU receipt 都存在时仍检查完整 stamp、revision、epoch 与 view。

缓存候选仍须通过本次成功原生调用的 exact current-call、Feature/config、command-list token/reset、资源和格式 gate，才可用于本次 typed admission。同步 CreateView 冲突和当前调用 gate 没有放宽；CPU 声明不升级 GPU `NativeUse`、`after_native_valid`、Execute/submission 或最终 Present 证据。

[Microsoft ClearUAV 合同](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearunorderedaccessviewfloat)
明确区分当前 shader-visible GPU 描述符、non-shader-visible CPU 描述符及显式资源参数。稳定的 CPU 描述符可说明本次 CPU Clear 请求的 typed format；它不证明 GPU 描述符内容或执行结果。

## 验证证据

- `descriptor-alias-runtime-cpu/result.json` 与 `cpu.log`：完整 production TU 的 CPU 合成 COM 测试通过 162 项断言，包含稳定 CPU UAV、GPU 槽淘汰、fresh-call gate、GPU proof 保持 unknown、同步冲突、资源/设备/heap/lifetime 反例。5 个 mutation control 均被捕获：`restore-gpu-pair-dependency`、`ignore-current-call`、`ignore-clear-revision`、`ignore-current-sync-conflict`、`skip-first-command-guard`。
- `descriptor-provenance-warp/result.json` 与 `warp.log`：WARP 共 450 项检查，另有 CPU 描述符注册表 1,688 项独立检查。真实夹具填充 65,537 个 provenance 槽淘汰原 GPU 槽，执行真实 Clear；CPU typed declaration 仍解析为 R8，fresh guard 通过，而 `NativeUse` 在实际 Execute 后仍保持 unknown。后续真实 readback 验证 Clear 写入纹理存储；RTV 目标/范围、生命周期、复制和 Execute 数组位置检查也通过。
- WARP 记录 `native_clear_request_is_gpu_proof=false`，没有 NVIDIA Feature18 或游戏声明；CPU 夹具记录无 D3D device、GPU submit、NGX 或游戏执行。

## 中止批次与最终批次

前三次整套尝试均保留为未通过证据：`build-r1` 版本 pins 未同步；`build-r2` 旧 `test_picture_integration.py` build script blob 断言失败；`build-r3` 旧数值 PE.10 断言失败，当前身份为 PE.11（`0.4.7.11`）。`build-r4` 只完成必要的测试与身份核验修正后全项通过，前三次不计入通过结论。

## 当前复测边界

实际中英文菜单最终通过：各 461 帧、8 阶段与 8 份 F8，每语 34 项自动检查通过；18 张原始截图逐张核对，目标控件和选中值可读，既有长诊断行右缘裁切及底部滚动内容单独记录。没有残留菜单宿主。

首轮中文在 189 帧、已生成 5 张截图时因报告按钮点击失败而 exit 4，英文未运行。状态切换后的布局移动使私有测试脚本的点击释放坐标失配；仅修正该脚本的按住重定位及后续释放后完成一次双语重跑。原失败完整保留，生产 Core 与冻结构建未改，不能据此宣称修复了 Core 菜单故障。

Corefix.6 复测聚焦绝区零：一次关闭再开启 NR、保持约 30 秒并采集一份 F8；见[中文指南](../../testing/CORE_047BETA_COREFIX6_ACCEPTANCE.zh-CN.md)和[English guide](../../testing/CORE_047BETA_COREFIX6_ACCEPTANCE.en.md)。

死亡搁浅 Corefix.5 仍无持续效果且偶有闪烁；2040 点完整 store match 只证明一次采样窗口的存储比较，不证明 Present 或最终显示。本轮不部署死亡搁浅 Core，也不要求重复四阶段测试。

真实 NVIDIA Feature18、游戏视觉和性能仍未验收；问题 #2、#205、#203 保持开放。不自动合并 main 或 release，不发布稳定版。
