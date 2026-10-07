# #330 有界显存观测（2026-10-08）

基线 `origin/main=024f9cc32778c1056db364d76d087ee257da6502`；本切片生产
`src` tree 为 `cfc4865940498feb09f70f6e6b1d8f8bf82a0467`。独立开发分支
`codex/330-memory-evidence-20261008`。不改变生命周期证明、预算准入、NR
Create/Release 决策、运行库、设置、UI、任何 GPU 命令或默认值。

## 实现与口径

- F8 在原 `GetResourceAllocationInfo` 去重账本上，增加 Core/aux 的 live、retired、
  pending context 身份、quarantine 标记、当前/历史最大槽数；每槽列出 source、
  Proxy、NeuralOutput、Result、SRIntermediate、MotionProxy、RR depth、其他自有资源。
  borrowed 和 aliases 不重复计费。失败测量有 unknown groups 和逐角色 unknown mask。
  可枚举 quarantine lower-bound 单列，detached quarantine 仍为 unknown。
- 槽高水位只在既有成功槽分配处更新；bytes 高水位仅为 F8 **已采样**高水位。
  extra、dual、privateSR、temporal、capture、replay、face 原 owner 预算并列呈现，
  明示单位/时点可重叠，不能相加，也不再加进资源下界。
- Feature18 Create、普通和 compat Release 各在既有冷事件前后，查询原 command-list
  device 的 adapter、既有 `CreationNodeMask=1` 对应 node 0 的 DXGI local usage。
  只保留最近 32 事件及 overwritten 数；记录返回码、时间、身份和有符号 delta。
  beforeValid/afterValid 各自按 attempted 和 HRESULT 判断；未尝试/失败的 usage
  为 unknown，成功查询所得 0 保留为 0。adapter/node 不同或 delta 溢出时 delta
  为 unknown。它是含并发噪声的
  **进程**观测，不是 NGX/model/plugin 分配，也不代表可回收量或 FPS 因果。
- 趋势每 600 个 enabled Present 且至少相隔 10 秒一次；DXGI 查询和日志在 Core 锁外。
  只消费既有 multi-layer primary NR 完成时间戳，不新增 GPU 查询、读回或命令；
  最多 120 个同 config/history/instance/queue/layer/extent 样本。每次报告消费区间，
  缺样本为 missing；普通单层没有既有 NR 时间戳时也为 missing，不借用其他历史。
- `allocation.complete=0` 不变；模型内部、PSO、descriptor heap GPU bytes 仍 unknown。
  逐 context 明细范围明确是 Core/aux 两张表；compat/FG 保留原分类总量，不声称
  完整插件显存总账或已完成 #329/#331。

## 有界容量与 CPU 验证

本地证据标识：`local-evidence:issue330-20261008`；下列回执相对此标识。

| 验证 | 结果 / 回执 |
|---|---|
| 实际 CoreSlot/CoreContext + allocation-info COM CPU 服务 | `cpu-r7/validation.json`：两正向 exit 0；11 个已编译精确负控各 exit 41；含去 context、去高水位、错角色、混身份/历史、跨 node、失败 usage 伪有效、missing 写零、错误脱敏及实际 trend 的去限频/重复旧区间 |
| 原账本 CPU / 真 append 锁 | `old-ledger-cpu-r1`；`append-64k-r2`：64 KiB reserve 线程、32 冷事件满历史通过；去反馈锁的已编译反例精确失败；本轮未运行 WARP |
| 完整 Core F8 CPU 格式化、脱敏、落盘与解析 | `f8-full-cpu-r5/validation.json`：7 clock cases 全 exit 0，known-clock 1769 checks，其余各 1374 checks，0 failures；event 两个 20 位 usage 和最大合法负 delta |
| 容量精确反例 | `f8-old-tail-cpu-r5`、`f8-old-metadata-cpu-r4`、`f8-old-text-cpu-r4`：各 7 cases 编译成功后精确 exit 41，分别命中容量 / all-sources oracle |
| Windows 完整 Core TU syntax | `final-syntax-r2/validation.json`：full face / pinned codec+aux 头，exit 0；8 个既有 warning；非链接或 canonical 构建 |
| portable | `portable-r2.log`：56 PASS / 0 FAIL / 1 缺历史 `captured.nrf` SKIP；跳过不计通过 |

实际 Windows ABI：Snapshot **3632 B**，ContextDetails **60160 B，仅 heap**，
ModelHistory **8720 B**；大 context/slot 数组不进入 64 KiB F8 worker 栈。
每快照保守预留 176 KiB；metadata 640 KiB、mandatory tail 337 KiB、single TXT 832 KiB。
完整 CPU 实测最大 metadata **595558 B / 655360 B**、必需 tail **285286 B / 349184 B**，
全部摘要文本 **782598 B / 851968 B**，无截断。字节大数通过窄 typed grammar 保留，
真实长句柄仍脱敏，secret 行仍删除。

失败保留：首轮 portable 漏设两项 compiler env（54/2/1）；首轮完整 F8 numeric fixture
错误期待长句柄原文不变（6 cases 通过但总 gate 失败），后分开验证数字保留和句柄脱敏；
`cpu-r4` 缺 test-only `<cstdlib>` 的编译失败；首次 old-metadata fixture 在确切容量失败后
仍解析截断字段，导致 bad_alloc/timeout。现仅在已经命中容量 oracle 后停止该负控 fixture，
正向全部检查继续执行，生产门不放宽。旧失败目录不覆盖。

## 合成验证接口与剩余边界

CPU runner 支持指定实际 assembled Core，fixture 来自 runner checkout：

```powershell
python scripts/validate-memory-observation.py --compiler <clang++.exe> --source-root <assembled-Core> --output <fresh-output>
python scripts/validate-feedback-timing-full.py --compiler <clang++.exe> --reshade <ReShade-root> --source-root <assembled-Core> --source-manifest <source-manifest.json> --output-dir <fresh-positive-output>
python scripts/validate-feedback-timing-full.py --compiler <clang++.exe> --reshade <ReShade-root> --source-root <assembled-Core> --source-manifest <source-manifest.json> --output-dir <fresh-negative-output> --negative-control old-tail-reserve
```

默认完整 F8 runner 一次跑 7 clocks；旧容量负控读取实际声明，合成 tail 361 KiB
不会因常量 anchor 改变而失配。其余容量反证选 `old-metadata-limit` / `old-text-limit`。

#341 自身增加的 24 KiB tail 必须保留：合成时先取 **361 KiB**，然后用实际组合源重跑
完整 F8 容量门，不能把本分支 337 KiB 结果当作组合通过。#338 capture Active 与新增
CollectTiming window 的交叉 CPU gate 在本切片初次冻结时尚未覆盖；合成必须验证普通完成样本、各
capture 状态 ResolveTiming/ResolveLayerTiming 后不增加正常成本样本、合法旧 recording
以及 idle 恢复，并以移除真实 Active guards 的编译后精确反例证明。

后续独立组合源 `a24e4fd88ff7985bb9e5bdfaf99f6d5ad99c1257` / src tree
`7fe3819be240c0fad0eb16526e9d4b3ce0a67f87` 已补该交叉 CPU 门：
`scripts/validate-memory-capture-timing.py` 读取实际 timing inl、capture Active、
TimingWindow 及已保留的 capture fixture，不复制另一套统计实现。35 项正向通过；
删 primary guard、删 layer guard、删 Observe 的三个副本先编译成功，再分别
精确 exit 1 命中指定断言（沿原 fixture 的错误码）。首轮 Queue 测试替身聚合
初始化顺序错误的编译失败保留，未改生产代码。该结果只证明 capture 与新增窗口
不会交叉污染，不是 GPU/NGX 或组合 canonical；实际组合容量仍须完整验证。

本轮未运行 GPU、WARP、NGX、canonical 全入口或游戏，没有打包、部署、GitHub 写入、
合 main、关闭 issue。资源逐项真实 WARP allocation-info 对账、同 adapter/node 的真实
Create/Release 游戏噪声样本及最终组合 canonical/验收由统一候选完成；不要求维护者
单独测诊断包，也不把 lower-bound 或进程 DXGI delta 提升为完整模型成本。
