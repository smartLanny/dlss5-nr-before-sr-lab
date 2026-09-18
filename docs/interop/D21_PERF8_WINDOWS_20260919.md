# Perf8 Windows 验证与维护者测试交付

实际构建来源：`7e2dfd2712c9d19ba44e629fa1d4dd05dd99e0c4`。增量基线为 Perf7 `a3ea8f9cbf3e06662e622852dc11581973798d18`。构建后仅补交付文档，四处生产文件不得变化。

## 已完成结果

完整 `build-perf8-candidate.ps1` 在冻结源码上从头运行成功，进程退出码 0。中文与英文 Core、owned companion 及 carrier 完整编译链接完成。最终 `PERF8-FINAL-VERIFICATION.json` 再次核验了所有门禁、源码字节与产物。

| 项目 | 实际结果 |
|---|---|
| 生成文件完整清单 | 229 个文件逐项 SHA256 核验 |
| 固定 Core 输入 | 164 项 |
| 完整 canonical 验证 | 46 个分组 |
| 原 Windows SRW / root-state / Perf3–Perf7 门禁 | 全部重新执行并通过 |
| Perf8 新旧状态/生命周期/范围差分 | 通过，旧工作量负控按预期拒绝 |
| native command-state | 67 项、64 KiB worker 与栈预算通过 |
| native record lifecycle | 46 项通过 |
| 完整 Core 描述符 CPU / WARP | 2225 / 669 项通过 |
| 来源传输 WARP | 1700 项通过 |
| 状态恢复 WARP | 24 场景及错误状态负控通过 |
| post-SR 快照 WARP | 1045 项、3 场景，错误像素负控通过 |
| 双语 Core LLVM 静态分析 | 零诊断；普通编译继承警告未隐藏 |
| 最终 shader 与已测 Perf7 比较 | 26 个对应 CSO 字节哈希全部相同 |

这些结果区分 Windows 运行、CPU 替身、WARP GPU 读回、NVIDIA 推理和游戏。新候选没有进行 NVIDIA 推理、游戏 FPS、弱 CPU、长时间兼容性或管理器 UI 导入验收。未部署进游戏、未发布 Release、未合并 main/stable。

机器没有 D3D12 调试层，显式采用已有 `AllowMissingDebugLayer` 模式。真实 WARP 读回与负控继续执行，`snapshot_debug_layer_verified=false`；不能声称 InfoQueue 检查通过。托管 CI 以本 PR 实际检查为准，不借用本地结果。

## 本轮失败与修正记录

第一次完整运行基于 `0166c278`，在独立 native-state 夹具编译时发现缺少新 `std::size` 所需的头文件，进程如实记录 failed。修复为不依赖额外头文件的 `sizeof(array)/sizeof(array[0])`，更新精确源代码 pin，独立 native/WARP 测试通过后提交 `7e2dfd27`。

随后以新输出目录重新从头执行完整构建，最终进程 passed/0。没有跳过 native 测试、修改失败退出码或用旧产物补成成功包。旧过程日志和回执保留。

范围 radix 的初版在反序及宽地址分布微基准中退化，未作为交付结果。当前版本使用反序处理、局部地址 radix 和比较排序的分布选择；未声称所有微场景都比旧实现快。

## 实际交付身份

版本：`beta0.5-dline21-perf8`。

- 中文 Core SHA256：`f1b6a35fd2d1df5ef8bd51fe3bf7411c8ec7b86c036a343a00956133f0414405`。
- 英文 Core SHA256：`e2db541d575d5ca1867c891c16077781f10440599e7ad6e9b7fa1cbe204f0a82`。
- 配套 nrchain SHA256：`1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2`，与 Perf7 相同。
- 中文更新包：`DLSS5-0.5-D21-Perf8-zh-CN-OTA.zip`，1,651,615 字节。
- ZIP SHA256：`407303d8e8f8a46010bd1a8606b35e0eeaaeb92e2bd838637203d401c29005b8`。

`nr-branch-ota-v1` 包仅包含一份中文 Core、匹配 companion、说明、许可证与清单。无 INI、模型、NVIDIA 游戏 DLL、调试符号或原始录制。打包器验证完整回执、PE/source/version、逐文件哈希及 ZIP CRC；独立最终核验再次读取 ZIP 和所有源文件。

四个实际编译生产输入的 SHA256：

| 文件 | SHA256 |
|---|---|
| `nr_source_state_table.inl` | `77e991033b50e242ad8fc375cb2177cc9f76324e7288c4db573997022746525f` |
| `nr_command_state.inl` | `4af71df4f218536f95039a020f66e5d987541f1712e27b050ac1d90aaf242bf4` |
| `nr_descriptor_provenance.h` | `fa9a9b2b5c7eeba3d541df09b3d7d257ee8829877fb9737cadab89ff737eecfa` |
| `nr_descriptor_provenance_runtime.inl` | `1ade49e337e8d333fb841b18084c7a419e6b7d3d58200c53e984e29ebdc8c05e` |

这些文件与冻结 Git 源码逐字节相同。范围 scratch 缓存增加每个活跃线程最多约 96 KiB 固定保留内存，重入/大批次保持 owned 回退；不是缓存 COM/GPU 资源。

## 下一轮验收

包旁附 `Record-Perf8-NRSR.cmd` 和 `Record-Perf8-SRNR.cmd`，各调用原录制器的现有 `-Root/-Seconds/-Repeat/-State/-Note` 参数，输出到有可用空间的捕获目录。两个 PowerShell 入口只完成语法解析；没有启动录制、自动按 F6、安装插件或修改 INI。

退出游戏后备份旧 Core/companion 到游戏目录外，只保留一份 Perf8 Core 与本包 nrchain，其他模型、运行库和配置保持一致。前置和后置分别使用已录 Perf7 的对应镜头、分辨率及 2/2/2 参数，避免跨路线计算版本增益。

每条路线录默认 OFF 15 秒→ON 15 秒→再次 OFF 15 秒，结束后保存同期 F8。开启态 NR 成功次数必须增长；首次初始化与稳态分开。无需再重录 D13/原版 D21/Reno 全套矩阵。

重点判断真实 Reset/状态索引重建是否减少、描述符规范化及分配是否下降，以及这些变化是否改善实际帧时间和再次关闭的 CPU 负担。首次 DLL/Feature18 初始化未由本版独立修复；不得将“热切换不卡”泛化为所有冷启动都完成验证。

持续旁路、无效果、闪烁或崩溃时回滚，收集 F8、Core 日志、版本与哈希上传 BUG。旁路后的高 FPS 不算性能通过。#277/#272/#273 保持开放、待复测，PR 保持草稿。
