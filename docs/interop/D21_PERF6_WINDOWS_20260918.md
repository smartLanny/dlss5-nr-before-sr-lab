# D21 Perf6：Windows 验证与测试包交付

## 冻结源码与产物

本次实际构建源码为 `510d6f85aedf9768d9d583ad6179017dd48e0a78`。
版本 `beta0.5-dline21-perf6`；数字 PE 版本继承原系列，须同时核对版本字符串、源码提交与 SHA256。
后续仅补充本记录的文档提交不改变已经构建的运行时代码。

| 产物 | SHA256 |
|---|---|
| 中文 Core | `4afac3e007545614e2ec2f145925c78bfa2e1228ad534aaab24787665126cbcc` |
| 英文 Core | `073e3849651e5b0b93e0b046bef0df9df7ca5a625dd73d6d9f9c24fbff4f67c4` |
| 配套 nrchain | `1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2` |
| 中文 Core-only 测试 ZIP | `fdd6184acf75abab240466968abcd1921833df4d4a205e8145c329d27da4cd9b` |
| 英文 Core-only 测试 ZIP | `418f482cf668b2e8d151da55fdf95ccd5b486f3a6f3b844e7a16f78622411837` |
| 中文 Core＋nrchain OTA | `075ab33eb9c0058823ddff66204f54119428f86e74823034e7954748518dfe2a` |

中文完整测试更新包为 `DLSS5-0.5-D21-Perf6-zh-CN-OTA.zip`，1,646,265 字节。
目录叶名 `d21-perf6-510d6f85-20260918-ota`，构建回执位于同级无 `-ota` 后缀的目录。
包内仅一个中文 Core、匹配 nrchain、说明、许可证及 `nr-branch-ota-v1` 清单。
不含 INI、模型或 NVIDIA 游戏运行库，不自动安装，不改变现有游戏。

ZIP CRC、成员集合、逐文件大小／哈希、Core x64 PE／内嵌版本及提交、companion 导出均已核对。
管理器界面导入没有测试，不能把包格式检查说成管理器功能验收。

## 已执行验证

使用 `scripts/build-perf6-candidate.ps1` 完成 Windows 全流程，退出码 0。
固定依赖沿用 Perf5 的 carrier、ReShade、DirectXMath 与 Clang 23.1.0 / LLVM-MinGW 20260826。
配套 nrchain 通过既有准确来源回执复用，与已测 Perf5 字节一致。

- 干净源码提交参与完整生成，228 个文件均重新计算 manifest 哈希。描述符头文件与 Git 对象字节相同；反馈文件与生成器明确插入 post-feedback include 后的预期字节相同。
- 完整双语 Core 编译／链接和 canonical 回执 46 个验证分组通过。
- 原 Lock2 同步／状态参数、Perf3、Perf4、Perf5 与新增 Perf6 门禁全部通过。
- 冻结 Perf5 的负控：256 个热键的重复查询产生 60,480 次链式比较；Perf6 热身后为 0 次链式比较，仍执行键和全部有效性检查。
- 已排序的 2,048 范围：旧比较器调用 4,102 次，Perf6 2,047 次。反序与任意顺序仍排序；精确地址空隙、类型和非法输入契约不变。
- 全部有效及未知 receipt 差分、容量淘汰、pending 保护、同地址 heap 更换、旧 Finish、资源替换与设备退休通过；150 组范围排序差分通过。
- 继承描述符随机差分 120,000 次操作、8,000 次多线程生命周期通过。现有描述符 CPU 2,225 项、来源策略 399 项通过。
- 完整生成 Core 的描述符 WARP 669 项通过；来源传输 WARP 1,700 项通过；既有状态恢复和快照实际 GPU 读回／错误像素负控通过。
- 最终 26 个着色器逐一重新计算哈希，与 Perf5 对应着色器全部相同。没有通过更换画质算法或降低工作量换取性能。
- 默认注册表占用 16,597,576 字节，仍小于原有 16 MiB 上限；描述符池容量未增加。

测试包含超出缓存大小的 4,096 键工作集，其微基准耗时基本持平。局部热缓存收益不能外推全部游戏调用、关键线程延迟或整体 FPS。F8 的新命中／未命中／链表步骤计数用于下一轮确认实际覆盖。

## 明确的验证边界

机器缺少 D3D12 调试层。本次显式指定 `-AllowMissingDebugLayer`：WARP 像素读回与错误像素负控真实执行，但 `snapshot_debug_layer_verified=false`。
未运行 InfoQueue／调试层验证，不安装系统组件或修改驱动来隐藏该缺项。
继承编译日志中仍有 novtable、HLSL 和未使用符号警告，不宣称整个构建无警告。

`source_verified=true`、`compile_verified=true`、`warp_verified=true`；
`game_runtime_verified=false`、`nvidia_feature18_verified=false`，未部署。
托管 CI 必须按 PR 的实际任务状态独立报告，不能拿本地通过充当 CI 通过。

## 下一轮测试

只需补 Perf6，同机器、同镜头、同配置：默认关闭 15 秒、开启单层 NR→SR 15 秒、再次关闭 15 秒，总计约 45 秒。
首次初始化与切换过渡保留在记录中但从稳态统计排除。录制结束后再保存 F8，避免反馈采集扰动稳定窗口。
无需重录 D13／原版 D21／Reno 四组矩阵。

保留现有 INI、ReShade、模型、桥接器和游戏设置，只安装本包一个 Core 与配套 nrchain；旧 Core 备份到游戏目录之外，避免同时加载。
重点看 `descriptor.slotQueries` 实际命中率和链表步骤、保留的 `descriptor.rangeQueries`、对应 CPU 热点以及帧时间。
开启时 NR 成功次数必须增长；持续旁路、无效果、闪烁或崩溃应回滚并携带 F8／Core 日志／版本哈希反馈 BUG，不能把跳过 NR 算作提速。

本版不声称解决首次 Feature18 初始化的秒级卡顿；也不通过 OFF 后台预热、关闭必需 hooks、删除 pending／代次／寿命检查来绕过问题。
