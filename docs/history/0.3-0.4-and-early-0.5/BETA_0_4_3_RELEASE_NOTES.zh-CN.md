# beta0.4.3 社区测试版说明

发布日期：2026-09-05
发布线：`codex/beta0.4.2` 非 DX11 D3D12 主线
集成顺序：#48 → #56 → #57 → release finalization

## 这版是什么

beta0.4.3 是 beta0.4.2 的稳定性、反馈取证与发布身份收口版本。NR → 游戏原生 SR 的默认处理顺序没有改变，也没有把 DX11 实验代码并入主测试包。

本版本优先级仍然是：不崩溃与安全旁路 > 正确性 > 游戏兼容性 > 性能 > 画质。

## 主要变化

### 1. bug sweep 与安全旁路

- 收窄 Lighting Protect 在高反差区域产生黑边或反向残差的风险。
- 修正遮罩相关参数处理与面板状态隔离。
- 原生 NGX 生命周期 Hook 增加保守重试路径，降低模块加载时序造成的永久失效概率。
- 空 PSO 和更高风险的扩展图形状态恢复仍默认关闭；无法证明状态可安全恢复时继续旁路原生结果。

### 2. F8 核心小包与 F7 图像包分层

- F8 生成独立 CPU 核心反馈包，不等待成功 NR 帧、不截图、不读回纹理、不为了采集修改画质设置。
- 核心 ZIP 有 8 MiB 硬上限，只读取已知日志位置；扫描受限时会明确标记 coverage 不完整。
- F7 会先生成与当前 session 关联的核心包，再进入图像采集路径。
- 核心包与图像包可以核对 session、源码 SHA、实际 addon SHA256、dirty 状态与配置代数；配对成功只证明记录身份一致，不表示截图与 GPU 阶段来自同一帧。
- F8 自定义热键不能与 F6/F7 冲突；新请求会清理过期结果路径，忙碌状态拒绝重复请求。

### 3. 反馈完整性与防错配

- 必需日志、字节计数、missing reason、complete 状态采用严格类型与交叉校验。
- 取消、无有效快照、元数据截断、扫描覆盖不完整、缺失或被拒输入时，不允许误报为完整包。
- 不同 addon 二进制不能只凭相同 source commit 被判定为 core/image 配对。
- 拒绝 ZIP 文件名 NUL 截断和非普通文件，继续保留成员白名单、CRC/SHA 与不解压执行文件的安全边界。

## 版本与包身份

beta0.4.3 release-finalization 将插件 UI 版本、`kBuildIdentity`、Windows File/ProductVersion、构建输出文件名、反馈采集器、打包器、CI provenance 和玩家更新说明统一冻结为 `beta0.4.3` / `0.4.3.0 beta`。

历史 `BETA_0_4_2_RELEASE_NOTES.zh-CN.md` 保持原样，避免把旧版本记录改写成新版本。

## 未改变的核心链路

- 默认 Mode 2：在独立 NR 工作分辨率运行 Feature 18 → 生成匹配残差 → 回写游戏渲染分辨率 → 调用游戏原生 DLSS SR。
- Mode 1 仍是游戏 SR → 全输出分辨率 NR 的质量/参考路径。
- NR 画质 / WorkMode 只控制 Feature 18 工作分辨率，不接管游戏自己的 DLSS SR 档位。
- RR / Feature 13 仍按现有保护逻辑旁路，不在本版本中重写所有权模型。

## DX11 边界

本 Release **不包含 DX11 实验分支 #46**。当前正式 beta 仍要求原生 D3D12 DLSS Feature 1 链路。DX11、无原生 DLSS 游戏与更通用 feeder/bridge 路线继续独立研究，避免把未完成的 API 兼容层带进稳定测试包。

## 验证状态

已完成或要求发布门禁覆盖：

- host bug regression harness
- Shader Model 5 编译与结构/数学检查
- D3D12 WARP 正负对照
- 命令状态、资源观察、stage capture、MinHook relay 回归
- LLVM 静态分析
- 中英文 addon 完整构建
- F8 核心协调器与反馈解析器测试
- Linux / Windows × Python 3.11 / 3.13 反馈完整性回归
- 最终 release commit 的干净 CI 构建与包 SHA256 校验

这些属于 source-level / compile / CI verification。没有真实游戏 F7/F8 长时采集、NVIDIA DLSSNR 模型端到端或全游戏矩阵的 runtime verification，不能把自动测试描述成实机已验收。

## 仍需实机关注

- #38 / #50：空 PSO 与 carrier 合同相关兼容性。
- #49：崩溃反馈仍需独立真实证据闭环。
- #51 / #53：版本与特定环境反馈仍独立跟踪。
- #47：反馈工具主线已进入 0.4.3，但最终游戏侧验收仍保持 open。
- 鸣潮、燕云、地平线、黑神话及其他曾报告间歇失效/崩溃的游戏需要当前 beta0.4.3 重新采集证据。

## 更新建议

本次优先发布轻量更新包。已有兼容 beta0.4.x 底包的玩家保留 `nrchain_nvngx.dll`、`nvngx_dlssnr.dll` 和现有 `nr_before_sr.ini`，完全退出游戏后替换 addon，并同步放置新版反馈工具。进入 ReShade 插件页后先确认显示 `beta0.4.3`，再进行游戏测试。
