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

## 后续最小范围

先完成一个准确 SR-off 功能首门，再串行 NR-on / RR；失败保留并定位，不扫配置。
真实 Feature18 Create/Eval/adopt 与精确退休成立后才加入画质与计量。画质和性能
独立进程/窗口；性能使用原 graphics queue/fence 的两个 timestamp，图像采集实际
工作为0，显存查询必须在 Sample 进程内。普通 Sample FPS 是 CPU loop，不能当
GPU帧时间。尚无可交付完整 MVP、Core实际增强画面或性能/显存结果。

另行准备的 raw低分NR→一次RR相位测试已发现旧 RGB 控制图经 Curve2逆映射的
完整量化区间全 unknown，未运行模型。仅允许一次有数学功效依据、模型前新封存
的灰阶测量设计；旧结果不覆盖、不缩误差界、不改 NR strength 或产品开关。
