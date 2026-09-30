# dev3 发布候选审查收口

对冻结源码 `9297042a965f7cb490f9841dbb11eafa508ffc2e` 及实际生成的 carrier 进行了运行安全、功能合同两路审查。以下是发现后的定向修复，不将旧构建结果替代最终候选验证。

| 发现 | 修复与证据 |
|---|---|
| P1：Feature13 的 NR context 未随三个真实 Release 入口退役 | 仅在原生成功或 FeatureNotFound、最外层调用时退休 Feature1/13；失败保持。实际生成函数54场景通过，旧 Feature1-only 负控检出遗漏。 |
| P1：carrier 把 NGX 访问异常转换为普通错误后继续处理命令 | Init/参数/Create/Evaluate 及设备创建不再吞硬件异常；普通返回码保留。Create/Evaluate 子进程真实抛出 0xc0000005 并以该码退出。只读诊断探针及可选 DRED 查询保留各自保护。 |
| P1：CPU Signal 假放行未完成 GPU 输出，超时后仍回收依赖 | 回填前核实真实 progress fence；失败隔离并保留资源；成功 Drain 后才能释放。35项生产函数状态测试及真实 WARP fence 完成/超时验证；不把 fake queue 测试称作 GPU 测试。 |
| P2：包内 README 仍沿用 dev2，当前状态链接失效 | 使用独立 dev3 玩家说明，统一仓库 CURRENT_STATUS 入口。 |
| P2：F8 无法带上 DX11 carrier 的真实错误记录 | 只读取唯一匹配已加载 carrier 同目录日志，沿用有界/脱敏采集，缺失明确记录；不改变 F8 单 TXT。 |

最终门禁由 `build-multi-api-candidate.ps1` 对同一 manifest 源码执行，结果见包内 `validation.json`。本地测试不代表 GitHub Actions 或游戏通过。

DX11 每帧新增 CPU/GPU 同步可能影响性能。500ms 帧等待、5s 创建/退役等待失败后，本进程不再重试，保留依赖直到退出。当前不承诺所有游戏兼容、全部格式/HDR/导引相位正确或长时稳定。Feeder、原生 Vulkan、Legacy 未进入候选。
