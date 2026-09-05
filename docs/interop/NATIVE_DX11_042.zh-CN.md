# 0.4.2：复用成熟时序输入，先贯通 DX11 原生 DLSS

关联 PR #46、Issues #4/#14。维护基线 `15909ef10914fcdf151ee193460451667fbf31dc`（`codex/beta0.4.2`）。本分支不依赖旧的 metadata-only PR #20/#21/#22；普通0.4.2构建不改，实验构建显式启用。

## 第一性原理与执行顺序

要把 NR 放在 SR 前，需要的是同一源帧的 Color、Depth、Motion、jitter、曝光、实际输入/输出区域及可靠提交顺序，不是游戏名字。FSR2+、DLSS SR 和许多 TAA 路径使用相近的数据，但“算法是时域”不代表所有引擎提供同一个可拦截接口；FSR1也不能混入这个判断。

一旦已有游戏模组正确提取这些输入，我们应复用其 DLSS 调用，而非重新写 TAA 检测器。当前最短路径：

| 优先级 | 输入来源 | 复用方式 | 当前边界 |
|---|---|---|---|
| 1 | DX11原生DLSS | 镜像真实NGX输入到D3D12，接现有NR→SR | 本PR已接运行时代码并完成首轮Windows构建；游戏未验证 |
| 2 | Luma或游戏专用DLSS模组 | 复用其DX11 NGX输出接口，走同一个桥 | 每款模组需单独验证；只有DLAA的不等于已能降低真实R |
| 3 | FSR2+/XeSS游戏 + 外部OptiScaler | 优先选已工作的DLSS输出；D3D12接现有核心，DX11再经桥 | 未接入或复制OptiScaler代码；组合未经本PR实测 |
| 4 | 没有成熟提供者的TAA游戏 | 再写单引擎适配，复用前面已验证的传输与NR核心 | 不提前建立第二套通用TAA框架 |
| 5 | 仅后处理画面 | 合成guide作明确降级的候选 | 本PR关闭此路径；不能假称真实游戏降分辨率 |

参考的一手实现：
- NIGos/dlss5-bridge，固定 `29baa4cc56e0aeb21219de16528e697b91522efc`（1.4.11）：https://github.com/NIGos/dlss5-bridge/tree/29baa4cc56e0aeb21219de16528e697b91522efc
- Luma（游戏TAA与DLSS集成）：https://github.com/Filoppi/Luma-Framework
- OptiScaler（输入/输出适配）：https://github.com/optiscaler/OptiScaler
- NVIDIA DLSS输入合同：https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md

这些证明可借鉴的路径存在，不证明本组合已经支持其游戏列表。OptiScaler作为独立外部互操作候选，不把GPL实现复制/链接进本核心。

## 本PR实际执行的链

```text
游戏或现有模组的 DX11 DLSS Color@R + 原生 guides
  → 同适配器共享纹理与双向 GPU fence
  → 桥的 D3D12 Feature1 调用
  → 0.4.2：R → N → NR@N → 修改回填R → DLSS SR → O
  → DX11游戏Output
```

成功路径不再同时重复跑原生DX11 SR；只有本次确实排入Output复制，才能跳过原生调用。queued copy只是CPU侧的提交事实，不等于GPU完成、正确显示或NR确实提交；后两者要从核心诊断、fence与实机图像分别证明。初次创建、部分区域输出、`skip_game=0`仍保留原生先执行的保守顺序。

桥保留ReShade的D3D12 device/list代理，让核心有机会正确观察状态和真实队列；没有清除核心任何 workset/fence/command-state 守卫。上游曾明确记录“某NR核心只成功一次而工作集不再回收”的现象，不能靠CPU等待或仅仅取消旁路检查解决。本组合能否持续回收，是首个实机放行条件。

## 实际修正

- 版本化CPU握手查询核心实际开关、模块观察状态及NR冲突；会话准入时可请求已有观察器重扫，避免错过首次Create。没有传递GPU裸指针，也没有伪造Feature18成功。
- 同一调用内判断是否排入回填；失败立即回到原生SR。按原生feature handle记录历史间断，恢复原生时设置Reset，调用后恢复原参数；失败不清除间断。16项表满或Reset未知时，不开放新句柄快路径。
- 原生深度转换失败拒绝占位深度；曝光QueryInterface或共享转换失败时拒绝继续。未实现的原生可选mask仍须列入逐游戏合同审查。
- F6/主开关关闭时不继续逐帧镜像复制/评估；初次私有设备/session初始化仍是一次性bootstrap，不宣称从未初始化过任何GPU对象。
- 关闭状态也读限频配置，避免`source=off`之后无法重新启用。
- 固定native DX11实验范围：关闭synthetic/Vulkan，保留游戏创建flags，不修改NGX驱动加载器的Feature18表。某驱动/NR运行库组合因此无法运行时应旁路，不自动换驱动或修改表。
- 原有NR模式、N比例、强度菜单保留；只在实验核心更正“DX11绝不可能运行”的提示。不新增虚假的全游戏R控制或FG/MFG选项。

## 构建

需要Python 3.10+、Git、Windows SDK/MSVC x64，以及0.4.2使用的LLVM-MinGW与ReShade头文件。精确下载/固定版本步骤见 `.github/workflows/native-bridge-042.yml`，已在Windows CI执行过，不需要手工改发布源码。

```powershell
git clone --no-checkout https://github.com/NIGos/dlss5-bridge.git external-carrier
git -C external-carrier checkout --detach 29baa4cc56e0aeb21219de16528e697b91522efc
python scripts/prepare-native-bridge.py --upstream external-carrier --output build/native-carrier
# $env:TOOLCHAIN_ROOT 与 $env:RESHADE_ROOT 指向工作流中固定的开发依赖。
./build/native-carrier/core/scripts/build.ps1 -ToolchainRoot $env:TOOLCHAIN_ROOT -ReShadeSourceRoot $env:RESHADE_ROOT
```

配套桥在VS x64 Developer Command Prompt中编译：

```bat
cd build\native-carrier\carrier\src
rc /nologo version.rc
cl /nologo /W4 /O2 /MT /EHsc /std:c++17 /Ireshade /LD dlss5-bridge.cpp version.res /Fe:dlss5-native-carrier-exp1.addon64 /link /DLL user32.lib advapi32.lib bcrypt.lib
```

脚本仅从已提交、指定身份的源文件生成新目录，不改工作树或游戏目录。核心和上游源码改变时锚点/哈希检查失败，必须重新审查；不能删除校验强行继续。生成核心标为 `beta0.4.2-native-bridge-exp1`、dirty，桥使用实验文件名，桥内上游版本字符串仍是来源版本而非签名身份；`source-manifest.json`记录每个生成源码的SHA256和双方提交。

源码归属不变，上游LICENSE留在生成目录；不提交或分发厂商DLL、模型、游戏文件。CI不上传二进制，只留源码身份清单。这不是已签名正式版，也没有完成安装器渠道集成。

## 测试记录与剩余验收

初始运行时提交 `64dc1dff1b18181c625ef97ac85d45333ac4fee7` 已通过Windows CI：
https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/33960305919

实跑项目为5项Python测试、真实固定源补丁应用、Windows ABI/历史测试、中英文0.4.2核心及nrchain编译链接、MSVC配套桥编译链接。日志有现有头文件/未使用函数警告，不称零警告。

后续加入 `tests/native_bridge/forward.cpp`，直接include实际转发体，用CPU驱动替身验证初始顺序、单次SR、失败回退、失败后Reset保留/恢复、处理中关闭、部分区域、Reset未知、配置关闭和原生失败。GCC与Clang ASan/UBSan本地通过；同测试加入Windows CI。它不是复制一份实现来测试，也不等于验证真实NGX、锁并发或GPU队列。后续提交的CI结果以该提交Checks/PR评论为准，不借用初始提交结果。

**没有进行本组合的真实游戏、Feature18 GPU、WARP渲染、图像或性能测试。** 首批候选是BG3 DX11原生DLSS、Skyrim SE的一种既有DLSS模组、Prey/Luma。候选来自上游路径，不是本PR新增的兼容承诺。只在离线单机环境测试，不绕过反作弊；本PR没有实现自动识别所有受保护进程的功能。

验收顺序：桥单独 → 本组合但NR关 → NR开。固定构建/运行库/驱动/R/O；先N=R，再降低N。每组验证NR真实连续提交与工作集回收、实际输出差异、基础帧时间、桥转换/NR/SR总成本；NR关应回原生SR。覆盖Reset、镜头切换、DRS/HDR、F6/F8、窗口切换、读档、退出以及至少10,000源帧。不要只看菜单、Present FPS或调用返回值。

若NR只运行第一帧，先修核心/桥的真实队列观察；若静态正常但运动错误，核对深度/MV/jitter/曝光/可选mask与映射；不能通过关闭保护或强行改强度判定通过。持续可靠后，才把这一组装进统一安装器，并接真实渲染R控制。最终产品可以是一个入口和菜单，不必为实现“单DLL”重复写成熟引擎适配器。
