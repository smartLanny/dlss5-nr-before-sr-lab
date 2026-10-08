# 私有同源3D twoRR 短门 v1

本包只准备 CPU 源码/编译与收据，不改产品、场景生成器或游戏，不自动运行 GPU/NGX。flat v3 的实际有效编辑已经单独复核封存；本 3D 包是新身份、新输入合同、新短门，不能继承 flat 的 3D/画质 PASS。

来源：已实际成功的 `minimal-scene-measure-prepared-r2` 自有场景宿主，继承原 canonical-r3 Core `45df68dc80138e89778e544d49f15109ec437a4f`；不是当前产品 head。SceneFrame/scene.hlsl/scene.cso/七平面/camera/projection/采样/材质/原oneRR实现均保持字节一致，不接失败的 default renderer observer。新增 child/composition/精确同recording退休来自本项目自有且已实际通过的 flat v2/v3 接缝，没有复制 GPL 参考源码。

唯一执行规模：一个新进程，2 warm + 1 observe，child RR3、NR3、原parentRR3，parentCreate1+childCreate1+3 frames=5个实际 recording。SceneFrame 初始parentCreate内dispatch1+三帧各1=4；childCreate不重复场景dispatch。R=N=1280×720，O=1920×1080，一层NR，无新renderer/perf/capture矩阵。

路线：原同frame raw R+guides → 独立child DLAA RR R/R → clean R → NR N=R/color jitter0/原raw guides明确approximate → edited R → per-tap ratio-before-shift/正权bilinear/-j → raw×ratio composite R → 原parent RR R/O。child 与 parent 独立 history，同 original device/native/init domain不重Init。原parent只换Color，其余typed getters合同保持。

SceneFrame 实际 raw alpha=1 (`0x3c00`)；原输出不改。新 composition 独立只允许已证alpha1编辑；`.75`/任意未知finite/NaNpayload整像素底图退回。所有identity/拒绝 return不写，`CopyResource(raw→composite)`保原bits；合法RGB写保原精确alpha1。相对flat v3实际HLSL只有固定alpha前提和注释变化，不泛化任意finitealpha，不加epsilon/clamp/保护。14数学/位型与13alpha反例单列，旧flat `.75`域对3D1必拒、新3D1域可正确编辑，不能靠改SceneFrame输出绕过合同。

CPU收据要求21张实际七plane输入的hash/有限值/depth/MV/normal/albedo/roughness检查、48byte constants和实际同frame/list/token/reset producer追溯；24张中间读回、3张O。每帧child/NR/final RR计数与参数/domain/runtime/真正lease/queue/fence/Reset严格核验。frame2须实际combine、oracle预测和实际RGB变化均>0、变化仅在合法域、合法值≤原1FP16ULP、全alpha逐u16及non-edit全RGBA bits不变。accepted是CPU预测域，actual changed是实际读回bits，不能冒称额外GPU写掩码。低覆盖/全fallback/任何首失败都停止，不重试、不叠保护。

`CRITICAL_SOURCE.diff` 是相对 measure-r2 的真实源码窄diff，`INPUT_CONTRACT.md` 说明独立alpha与初始dispatch合同。`prepare_source_cpu.py` 是第一阶段接缝准备脚本；最终host snapshot/源码hash/diff/manifest为审查对象，封存后不得在原目录重生成。新3D C++宿主因真实接缝变化编译一次；SceneFrame CSO直接继承，不重新编译场景。shader-only编译单独记录。

root 在冻结包窄核后串行执行 `python run_scene_two_rr.py --root-exclusive-gpu-go`。固定新 sibling 输出 `minimal-scene-two-rr-v1-r1`，存在即拒绝；无flag及其它CLI模式在device/bootstrap前拒绝。实际结果需要独立CPU收据复核。准备阶段GPU/NGX=0；3D功能、画质、时域稳定性、性能和游戏验收均未预设通过。

真实运行的 launch/进程 guard 在本包 `runner_support.py`，不调用旧 helper 的 timeout kill。guard 包括新scene-two-rr、scene-bench、clang等已知干扰进程；只检测，不停止其它进程。超时保留PID及过程receipt，外层STOP-unsealed并退出，不对仍可写输出封存MANIFEST、不终止或重试child。`CPU_RUNNER_CONTRACT.json` 以假活进程走实际timeout分支证明未调用kill/terminate/wait，没有创建测试进程。旧模块只导入source/file/runtime/seal安全函数；真实调用闭包在 `RUNNER_DEPENDENCY_CLOSURE.json`。
