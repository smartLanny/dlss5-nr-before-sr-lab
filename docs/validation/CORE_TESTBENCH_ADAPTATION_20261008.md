# 官方场景测试台：接入状态与编译纠正

2026-10-08；开发侧隔离测试台，不是产品新路线或游戏验收。

固定官方 NRD-Sample `00fd99e8b36b4508544f0cd113dfc20c16094f14`、
NRIFramework `f9bddb7e79b91fb8017d3421e9bf44a584dc33dc`、NRI
`a08ed54e218ac3cf1fdd2d159b8d9d3034079361`。与 RTXPT 的成本比较后选择直接
NRI→NGX 接缝，先 CornellBox 单场景、固定镜头、一个 SR 或 RR parent、Core NR
开/关。官方 NRD 不等于 Feature18；必须核当前 Core/模型的实际采用。

原 Sample 与窄接入副本已完成一致 MSVC ABI CPU 编译。保留原 NIS 两对象/pass
和 SHARC dispatch，仅关闭反馈消费/动画；设备用 legacy barriers，Core 门不变。
当前实际 Core 为本地完整 canonical 的45df英文产物 `401c19ae…8375`；Feature18
仍为原 a2d16、bridge1acf。官方 native SR/RR310.9.1 是新 bench tuple，未替换
游戏运行库，也不能继承旧310.5/066的模型验证。

## 撤回 r2/r3 的实际编译准入结论

源码中已写 selected capability、官方 optimal 和 Init后/Create前 Query 接缝，
但此前只把私有宏传给最终 `NRI` target。实际包含 UpscalerInterface.hpp 的
Shared.cpp 由独立 `NRI_Shared` 编译，未收到宏，所以 r2/r3 实际 DLL **没有**
新增门或初始化诊断。源码/产物 hash 一致与 CPU 构建成功均不足以证明门已编入。

实际 SR-off r2/r3 两次进程均 exit1，无原生输出/Feature18/画质或性能通过。
只证明准确 Core 被 ReShade 加载、Init前 flags0 查询为 Disabled。r3的 Sample
层记录 selected support=1、CreateUpscaler 返回1/null；缺 NRI 日志是编译缺口，
不能据此断言未进入原函数、NGX Init失败或 runtime 不兼容。原退出因果仍 unknown。

最小私有 CMake 修正是作用于真正的编译目标：

```cmake
target_compile_definitions(NRI_Shared PRIVATE DLSS5_TESTBENCH_ADMISSION=1)
```

后继 r4 必须核实际 Shared.cpp 编译命令、预处理/ASM→OBJ→最终PE中的控制流，
并以旧 r3 缺门为反例；不再只核源码和文件 hash。不改 Init参数/AppID/空path、
运行库、质量、Core 或门。所有旧包、执行失败与短核报告保持原件，新纠正单独记录。

r2 receipt SHA `75f7ff6709b697c8b595767af599cb24a3cc95f27c3d3966b8b3e5f148e77e09`。
r3 receipt SHA `a68db2d913f82b5874eb8964e0dc0dbc819afbc88395e056fb612480b16aa6e5`。
独立纠正报告 SHA `9755b83f755557aef988027510a127da8e1461b8f9f09988588ca41455a33741`。

## r4 实际初始化失败已定位到调用边界

r4 已核真正 Shared.cpp 编译命令及 OBJ→最终 PE 中的 Init/caps/回调/optimal
控制流；最小编译目标修正成立。实际 SR-off 首次运行仍 exit1：静态 SDK 的
`NVSDK_NGX_D3D12_Init` 返回 `0xBAD00001 / FeatureNotSupported`，随后 NRI
Create 返回 FAILURE/null。caps、Init后回调、Feature Create/Eval 和渲染均未到。
准确 Core 已加载，Init前 off 查询仍为 Disabled。没有重试或运行其他组合。

此返回是广义初始化拒绝，不能锁定 AppID、空 dataPath、设备或运行库。官方
v310.9.1 头文件规定 numeric ID 须由 NVIDIA 分配；无该 ID 时使用自有
ProjectID/CUSTOM，dataPath 需可写，API 默认仍为0x15。已成功的 Core 宿主走
ProjectID/显式目录的 bridge 初始化，与本次 numeric/空目录/静态 SDK 不是同输入
正控；也没有已成功的新310.9.1 parent 证据。下一窄改动只启用本进程 NGX 日志
callback，保留原 Init 输入和 tuple，先取得因果日志再决定适配修正。

r4 执行结果 SHA `22887ce6c66091428fb85e42112999fa220634160919dcb7e6d127a9c5b55bff`。
独立 Init 合同方向 SHA `b8d7ca3c1462f729c423c728097a9a0ede030eb55347da652d35300469fd9147`。

## 后续最小范围

先完成一个准确 SR-off 功能首门，再串行 NR-on / RR；失败保留并定位，不扫配置。
真实 Feature18 Create/Eval/adopt 与精确退休成立后才加入画质与计量。画质和性能
独立进程/窗口；性能使用原 graphics queue/fence 的两个 timestamp，图像采集实际
工作为0，显存查询必须在 Sample 进程内。普通 Sample FPS 是 CPU loop，不能当
GPU帧时间。尚无可交付完整 MVP、Core实际增强画面或性能/显存结果。

另行准备的 raw低分NR→一次RR相位测试已发现旧 RGB 控制图经 Curve2逆映射的
完整量化区间全 unknown，未运行模型。仅允许一次有数学功效依据、模型前新封存
的灰阶测量设计；旧结果不覆盖、不缩误差界、不改 NR strength 或产品开关。
