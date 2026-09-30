# D21 Perf4：Windows 验证与可测试产物

## 构建身份

实际构建源码为 `13cca5a29fc3e045a65cc8fa67b11972850cb7cf`，继承已测 Perf3 `c144d7cb`。
版本字符串 `beta0.5-dline21-perf4`，普通非 profile 构建；仅修改两个生产头文件。
构建后追加的 OTA v1 清洁源码字段及此交付文档不改变 DLL/着色器。

| 产物 | SHA256 |
|---|---|
| 中文 Core | `bb7d907dc24924be46a58c89c0054a1aa2b1a4e9cab87e8afe68a430bb12acf4` |
| 英文 Core | `ae6b54985c7c3e476e5de2b32b62d2a18b832e074dd6c7d1ad5a1b93eef8dcf8` |
| 配套 nrchain（与 Perf3 相同） | `1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2` |
| 中文 Core-only ZIP | `8af68e98c5583dc222ac923381d63532fd22ceebf80844409b678259887b9d2e` |
| 英文 Core-only ZIP | `7c4fb564378108abec189c1744cbab26ac436eb7f1baf57d47e3c31146f25114` |
| 中文 Core＋nrchain OTA | `52363adc44466819a516e3295fdc9a6ef5cfe78e7db1c8c672308497c028b1ad` |

推荐文件：`DLSS5-0.5-D21-Perf4-zh-CN-OTA.zip`，1,641,731 字节。
交付文件夹叶名 `d21-perf4-13cca5a2-20260918-ota`；完整本机路径由维护者交付消息提供。
只含一个中文 Core、自有 companion、中文说明、许可证和 `ota-manifest.json`；无 INI、模型、NVIDIA 游戏 DLL 或调试映射。

## 已执行验证

Windows 构建流程于 2026-09-18 08:17:13 UTC 完成，退出码 0。
依赖固定为 carrier `28aed409`、ReShade `4a50d1ed`、DirectXMath `28929ac6`，LLVM-MinGW/Clang 23.1.0 与 PowerShell 5.1。
使用已有严格 codec 缓存校验，源、选项、编译器完整 PE（仅允许时间戳差异）和每个 shader 哈希均被验证，没有绕过任何 GPU 测试。

- 完整生成 228 个 manifest 文件，全部重新计算哈希；两个生产头文件与冻结源码逐字节一致。
- 26 个最终 shader 与 Perf3 相应文件的哈希完全相同，实际产物逐文件重新核验。
- canonical 回执 46 个分组通过；完整中英文 Core 编译链接及 carrier 构建完成。
- 原 SRW/Lock2/Perf3 门禁继续通过；新增 Perf4 多 heap、多 range、满表失效测试的旧实现超预算、新实现通过。
- 同一测试：候选检查 245,888→512；饱和后的标签写入 4,096,000→256；批次表检查 2,048,000→320。二分比较不包含在候选计数中；保留完整调用微基准，不将计数当成 FPS。
- 3000 步饱和后重新发布、失败提交、退休、Reset/重新登记、对象复制与周期性全 256 标签核对通过；既有差分也继续通过。
- 原描述符 120,000 操作差分和 8,000 多线程生命周期、描述符 CPU 2,225 项、来源策略 399 项通过。
- 完整生成 Core 的描述符 WARP 669 项通过；来源传输 WARP 1,700 项通过。
- 原状态恢复 WARP 24 场景和负控、快照 WARP 1,045 项/3 场景及错误像素负控继续通过。
- OTA 的 CRC、精确文件集合、每项字节数/哈希、x64 PE、编译版本/源码身份和 companion 导出已核对。未部署到游戏。

## 未通过范围不是自动通过

机器缺少 D3D12 debug layer。本机显式延用 `AllowMissingDebugLayer`，实际 GPU 字节读回和负控仍运行，但 `snapshot_debug_layer_verified=false`。
没有安装调试组件或改变安全设置；默认不传开关仍严格要求调试层。

构建日志保留既有 HLSL、未使用函数和上游 novtable 提示，不声称日志零警告。
托管 GitHub CI 以实际检查为准，不能借用 Windows 本地结果；新包的 NVIDIA 推理、真实游戏、弱 CPU 帧率、长时间稳定性和管理器界面导入未在本轮完成。

满表仍会拒绝无法记录的新来源，缓存只减少重复的失败成本；不声明来源容量问题被改成成功。
首次开启/关闭瞬间的卡顿没有单独修复。当前热点机制的微基准收益不等于整帧收益。

## OTA 的 sourceDirty 字段

既有 OTA v1 协议要求布尔 `sourceDirty=false`。此字段在本包指向构建开始时已冻结且清洁的集成 checkout。
额外写入 `generated_source=true` 和 `sourceDirty_scope` 区分生成树；F8 的生成 Core sourceDirty 位可能不同，不能仅凭该位判定 DLL 被换过。
真正识别测试包应核对 `sourceCommit`、版本字符串和实际 Core SHA256，不用数字 PE 版本单独区分。

## 维护者测试

退出游戏，旧 Core/nrchain 备份到游戏目录外；仅加载一个 Perf4 Core，使用匹配 nrchain。
保留当前 INI、ReShade、桥接器、模型和画质设置，别混用 D13 的旧小 nrchain 或同时加载其他 NR Core。
优先在相同镜头与参数下补一份 Perf4 OFF→ON→OFF 与现有 Perf3 对照；开启时确认 NR 成功计数增长，切换长帧单独看。
持续旁路、无效果、闪烁或崩溃需回滚并上传 F8 TXT、Core 日志和哈希反馈 BUG，不能以旁路帧率通过验收。
保持 #277、#272、#273 开放，等待新包的实际反馈。
