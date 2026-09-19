# D21 Perf3：Windows 构建与测试交付

## 冻结身份

构建源码：`8ea3206fb91a642888c3c00ea830416db414001c`。
基线为 Cursor 已测 Lock2 分支 `63efc2bd5f55a4e9035b8b454dec5a8bb598f5d1`。
生成版本 `beta0.5-dline21-perf3`；数字 PE 版本继承 0.5.1.23，必须同时核对版本字符串、提交与 SHA256。
本记录是在构建完成后补充的文档，不改变已构建的运行时代码。

| 产物 | SHA256 |
|---|---|
| 中文 Core | `ec3e432e84fa1ed44359220f266f44cc0cf547d9db913fd8ad478da5c8a4a2df` |
| 英文 Core | `ae827e8c6ff50e93efbee1917f72b5bfa3a229d096b1c72201b5ea8e00184d28` |
| 配套 nrchain | `1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2` |
| 中文 Core-only 测试 ZIP | `31efc3963f8ff17b2a7ca122ccf38ab897293897b647ea38446ed119c1fd778c` |
| 英文 Core-only 测试 ZIP | `44851c6915a3c7c9788ffb58dbdd97b523132cdfc39aad1a2f9ee8e6ce621492` |
| 中文 Core＋nrchain OTA | `65cd57967732fb691a18cb39dd6eea8dde1f5bf763bfc55f097f3802e36ede7c` |

推荐测试包文件名：`DLSS5-0.5-D21-Perf3-zh-CN-OTA.zip`，1,641,049 字节。
包内一个中文 Core、配套 nrchain、许可证、中文说明与 `nr-branch-ota-v1` 清单。
ZIP CRC、文件名集合、逐文件字节数／SHA256、Core 内嵌版本／源码提交、x64 PE 和 companion 导出已核对。
管理器界面导入未测试，未自动部署；不含 INI、模型或 NVIDIA 游戏运行库。

本机交付根目录叶名为 `d21-perf3-8ea3206f-20260918`，OTA 单独在同级
`d21-perf3-8ea3206f-20260918-ota`。完整机器路径不进入仓库文档，维护者交付消息提供实际位置。

## 已执行验证

实际执行 `scripts/build-perf3-candidate.ps1`，固定依赖：
carrier `28aed409`、ReShade `4a50d1ed`、DirectXMath `28929ac6`，LLVM-MinGW 20260826 / Clang 23.1.0、Windows PowerShell 5.1。
使用已核验源码与 SDK 回执的 nrchain 重用路径；该 DLL 与 Lock2 相同。

- 完整生成：228 个文件全部重新计算哈希；163 个固定 Core 输入；两个修改头文件与冻结源码逐字节一致。
- 两个实际生产头文件的新旧负控／正控通过。8 个描述符失效范围的槽访问为 8,192 → 28；四列表热缓存场景的全表比较为 1,000,000 → 0。这是确定性工作量，不是游戏提速比例。
- 描述符新旧差分 120,000 操作及 8,000 次多线程生命周期验证通过；新增嵌套、pending、未对齐、高地址、重叠和源状态差分通过。
- 现有描述符 CPU 2,225 项、来源策略 399 项通过。
- 保留的真实 Windows SRW 27 项、Lock2 根参数／缓存回归通过。
- canonical 构建回执的 46 个验证分组通过，涵盖来源身份、格式／曝光／MV、RR、状态恢复、捕获、shader 和完整双语编译。
- 中英文 LLVM 静态分析各为零诊断。普通编译日志仍有继承的 HLSL 提示及上游 novtable 属性警告，未称整个日志零警告。
- 状态恢复 WARP 24 个场景以及错误混合状态负控通过。
- 编码、颜色和分层等既有 WARP 场景与负控通过；最终 26 个着色器逐项重新计算哈希，与已测 Lock2 的对应着色器全部相同。
- 完整生成 Core 的描述符 WARP 669 项通过，检查真实 cached-view clear、RTV、范围／寿命／拷贝／批次位置和像素。
- 来源传输 WARP 1,700 项通过。
- post-SR 快照 WARP 1,045 项、3 个场景及故意错误像素负控通过。

## 明确未完成的部分

机器没有 D3D12 调试层。本次显式传入 `-AllowMissingDebugLayer`，仍执行实际 WARP 字节读回和错误像素负控；`snapshot_debug_layer_verified=false` 单独写入回执。
缺少 InfoQueue／调试层验证，不能描述为调试层全绿。默认不传此开关时仍严格要求调试层。
没有安装 Windows 组件、改驱动或系统安全配置。

未运行新候选的 NVIDIA Feature18 推理、实际游戏、弱 CPU 性能或长期稳定性验证。
没有声称减少局部遍历次数就等于整体 FPS 按比例增加；没有把安全旁路当作提速。
GitHub CI 状态以 PR 的实际 checks 为准，不能借用本地通过状态标记托管 CI 通过。

## 测试方式

退出游戏，把旧 Core 和 nrchain 备份到游戏目录外；只留一个 Core，不同时加载旧 D13、D21、Lock2 或 RenoDX NR Core。
保留现有 INI、ReShade、桥接器、NVIDIA DLL、模型及画质参数。使用包内匹配的 nrchain，不要搭配 D13 的旧小体积版本。

用上一轮相同镜头和参数比较 Lock2 与 Perf3：默认关闭、开启单层 NR→SR、再次关闭。
开启段确认成功计数增长；初始化与关闭切换的长帧单独记录，不混入稳态。
Perf3 没有宣称解决首次初始化卡顿；复杂场景、Reset／重建、RR／FG 和跨游戏验证仍需覆盖。

若有旁路、无效果、闪烁或崩溃，回滚并上传 F8 TXT、Core 日志及版本／哈希。
#277、#272、#273 保持开放，待新候选实测后再决定状态。
