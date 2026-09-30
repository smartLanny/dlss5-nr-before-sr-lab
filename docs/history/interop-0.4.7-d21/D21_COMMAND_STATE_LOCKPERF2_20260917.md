# D21 lockperf2：参数写入与注册表查找修复候选

关联 #277；同时覆盖 #272/#273 的常驻状态维护候选路径。最终研究集成目标仍为 `next/0.5`，不合入 `main` 或稳定 0.4。本次新增运行时代码只改 `src/nr_command_state.inl`。

## 基线与范围

开发前读取线上 main `dae86e9898399c20e52ada54a5ec4350da4d4fa1`、open Issues/PR、研究分支和近期提交。实际测试继承点为 `d8de528f4df23999c4f6711320a94ac522e0f44e`，即 lockperf1 加既有构建修复及 SOTTR wrapper-alias 修复。保留这些祖先；不混入独立的 D22 Core Create 修复、PR #276/#278/#279 的画质、菜单省电、五层统一改动。

弱 CPU 反馈指出 D21lock 出帧/GPU 利用率低于 D13，但现有轻量日志不足以证明全部回退来自同一函数。本次修复源码里可直接复现的两类额外工作，不宣称已经定位全部游戏根因，更不把 CPU 总占用降低当作帧率提升。

## 实现与安全边界

### 根参数：不清空未使用的常量载荷

地址和描述符表 setter 原来每次清空整个 `CommandRootArgument`，包括 64 个 UINT 常量。现在只写 kind、value、constant_mask。切换 Root Signature 时逐项作逻辑失效；heap 变化仍使两阶段 Table 参数失效，保留非 Table 参数。

恢复端先判断 kind，Constants 再按 constant_mask 逐段回放；反馈仅统计非 Empty 项。因此已经初始化、但不再有效的载荷字节可以保留。每次非 Constants -> Constants 都清零旧 mask，防止局部写入复活先前常量。相同 Root Signature/heap 的保留规则、unknown-domain 重新绑定、无效输入拒绝不变。

已审查消费者：`RestoreNativeCommandState` 的按类型/掩码读取、`CaptureNativeCommandState` 的 evidence 统计、`CopyNativeCommandState` 的已初始化字节复制。没有加入按未使用字节比较或序列化的消费者。未来新增消费者必须遵守同一规则。

完整构造、Reset/Release 的初始化、COM 引用、RTV/DSV 私有副本、大状态快照、恢复顺序保持原样。本次没有做 union、脏区域复制或任意丢弃恢复字段。

### 注册表：跨 setter 共享四项线程缓存

旧单项 TLS 定义在 `MutateNativeCommandState<Mutation>` 模板内部，不同 lambda/setter/hook family 会各持一份。列表切换时每份都可能重新扫描最多 2048 个 record。

新缓存为单个非模板 thread_local 对象、四个固定条目，保存 native/epoch/record，负命中同样有界。最近项命中只做常数次比较；未命中才扫描。超过容量仅退化为扫描，不丢状态。没有在生产热路径加计数器、计时或日志。

沿用原有 registry shared -> slot exclusive 锁序。拿到 registry shared 前绝不解引用缓存 record；等锁后重新取 epoch/查缓存。记录/state 的退休、槽位和地址复用仍由原 exclusive 生命周期操作和 epoch 发布保护。hooks_ready 在锁内每次重查；没有用裸指针充当寿命凭证，也没有 try-lock/drop-update 路径。

### 未改动的候选

Post-boundary 观察已有活动条件早退，描述符/源帧观察有独立消费者。本轮不按 F6 状态粗暴卸载观察器，避免开关后恢复信息断档。大状态复制没有被现有证据证明是首要热点，本轮保留。原 registry 锁仍存在，不宣称已经消除所有同步或关键路径延迟。

## 已执行的反馈闭环

`validate-command-state-hotpath.py` 从固定 d8de528 Git 对象提取旧生产函数作负控，从当前文件提取新函数；不维护第二套手写被测算法。扫描计数只注入临时测试源，不进入插件。

- 测试前旧实现：四列表、两种 setter、16,000 次调用，16,000 次 registry 扫描，超过预算，退出码 3。
- 修复后同序列：4 次扫描。四个未观察列表也只有 4 次扫描。
- 覆盖负缓存转注册、同 native 地址换 state/slot、容量溢出、hooks_ready 无 epoch 切换，不允许丢写入。
- 旧 scalar setter 清零 64 个未使用载荷字；新 setter 保留全部 64 个，类型、value、mask 正确。该指标是确定性存储范围检查，不是 FPS 测量。
- 20,000 组固定种子的类型/根签名/heap/局部常量状态序列与旧实现的有效状态一致；覆盖 bit63、无效范围/空指针及 COM 引用平衡。
- 原有 27 项实际 mutation/descriptor-reader 同步场景继续通过，含同列表互斥、不同列表并行、capture/reset/retirement 阻挡、异常退出及寿命变化。
- 上述 C++ 场景在 macOS Clang 21 上带 ASan/UBSan 编译和执行。对象为 CPU/COM 替身；Unix 同步替身不是 Windows SRW 或 D3D12 验收。
- 使用真实 Core 与固定 carrier 后，multi_api 71/76、native_bridge 18/22、UI/clipboard/F8/RR 43/44 通过，共 132 项通过、10 项因 Windows/完整运行依赖跳过；跳过不计为通过。
- 实际创建观察边界 61 项断言、native Reset 边界 28 项断言通过。两个旧测试夹具的全静态链接标志仅在 macOS 下移除，测试逻辑与 Windows/Linux 标志保持不变，未以跳过测试掩盖失败。
- 完整生成流程输出 228 个受 manifest 哈希保护的文件，163 项固定 Core 输入。逐项哈希核对、新 command-state 文件与工作源码的字节一致性、候选版本和 source_commit 核对通过。生成源不等于 DLL 编译。
- 追加 5 项实际 CI 路由/门禁保留/身份检查及 1 项 archive 身份测试通过；三个修改的 YAML 文件解析通过。它们是源码路由检查，不是 Actions 或 PowerShell 运行验收。

复现命令（任选可用 C++20 编译器，Python 3.10+，使用全历史 Git checkout）：

```text
python scripts/validate-command-state-hotpath.py --compiler clang++ --sanitize --output <new-output-dir>
python scripts/validate-command-state-lockperf.py --compiler clang++ --sanitize --output <another-new-output-dir>
```

## 生成与交付

`prepare-multi-api-beta.py` 固定新 command-state blob，并把完整生成源、构建描述和资源字符串标为 `beta0.5-dline21-lockperf2`。数字 PE 版本仍继承 0.5.1.23；区别候选必须使用字符串版本、source_commit 和 SHA256，不能仅看数字 PE 版本。

Windows 使用 `scripts/build-lockperf2-candidate.ps1`。旧 `build-lockperf1-candidate.ps1` 名称保留为转发入口，但不再把新 DLL 错标为历史 lockperf1。继续使用此前构建的五个路径参数：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-lockperf2-candidate.ps1 `
  -UpstreamRoot <pinned-carrier-checkout> `
  -ToolchainRoot <llvm-mingw-root> `
  -ReShadeSourceRoot <pinned-reshade-checkout> `
  -DirectXMathRoot <pinned-directxmath-checkout> `
  -OutputRoot <new-output-directory>
```

新入口先跑真实 Windows SRW 回归和两项热路径负控/正控，再调用原完整双语构建与 WARP 门禁。任何失败都不得打成可测试成功包。允许继续使用既有 `-NrchainReuseRoot` 的精确来源校验，不降低复用校验。

成功后产生 `TEST-zh-CN` / `TEST-en`、各自单 Core 的 `DLSS5-D21-lockperf2-<language>-test.zip`、`LOCKPERF2-CANDIDATE.json` 和不放进游戏包的 `PRIVATE-SYMBOLS`。不附 INI、NVIDIA 模型或替换桥接器，不自动安装、发布 Release 或覆盖旧包。现有可复用 CI 接入同一测试/构建入口，没有新增 pull_request/push 触发器。

PR 创建后核查真实路由，发现 `next/0.5` 原本由 `beta050-validate.yml` 走旧 dev4 原始源码构建和双语言/INI 混合打包，不会到达新的生成器。因此该研究工作流保留原 host 与 shader 前置检查，把完整构建 job 转发到 canonical multi-api 工作流；旧 Windows job 中 canonical 尚未覆盖的所有 validator、command-state 负控和 post-SR WARP 负控保留在 lockperf2 入口。不是删除检查换取绿色状态，也不再并行产出一份误标的原始源码包。所有 checkout 取同一 PR HEAD 并保留冻结负控所需历史。

## 验收与限制

云端 ChatGPT 轮次没有连接 Windows；GitHub Actions 仍因账单/额度未启动，不能记为通过。后续本机 Windows 轮次已在隔离 worktree 完成双语 Core 编译、canonical WARP 和测试 ZIP，源 HEAD 为 `1bacd72c2cf383013c78c0516c45d99691adbe91`。官方 `build-lockperf2-candidate.ps1` 因本机缺少 D3D12 debug layer（`0x887A002D`）未能一次跑通 `post_sr_snapshot_gpu`；该缺口仍开放，没有跳过该检查去改脚本出“成功包”。NVIDIA Feature18 与游戏实测仍未做。

候选构建后，先原弱 CPU 场景冷启动 F6 OFF，再 ON -> OFF、单层 NR -> SR / SR -> NR，双层单独测试。保持 INI、分辨率、SR/RR/FG 与模型相同，确认只加载一个 Core。比较同窗 Present 帧时间与 GPU 行为，而非仅比较进程总 CPU 或采样热点排名。

出现安全旁路、效果消失、闪烁或崩溃，回退原包并上传 F8 TXT、Core 日志及候选身份。安全旁路不是性能验收通过。#277/#272/#273 在真实复测前保持开放；本次不改变 #223 的兼容验收状态。
