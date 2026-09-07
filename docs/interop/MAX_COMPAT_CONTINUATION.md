> 当前状态以 [CURRENT_STATUS.md](CURRENT_STATUS.md) 和 PR #133 最新 HEAD 为准。以下为历史设计/实施记录；不要根据旧未勾选项重复开发。本批交付测试候选后等待维护者进一步指示。

# 最大兼容性支线：持续开发与最终统一验收

## 本轮授权与唯一入口（2026-09-07）

仓库 `smartLanny/dlss5-nr-before-sr-lab`，持续集成分支 `codex/multiapi-045-integration`，Draft PR #133，目标 `release/0.4.5-test`，执行看板 #80。不要从旧 #79/r3 或移动 main 重建另一条重复的长期兼容线。

维护者明确：尽可能把最大兼容性需求做完整，再由维护者统一进行游戏测试。**源码开发、主机/编译/自动测试不以逐游戏复测为前置许可；游戏未验收必须诚实保留，但不阻止下一个独立模块开发。**最终是否合并维护线/主线和发布由维护者决定。

**本轮不做 MFG 兼容、解锁、倍率 UI 或新的 FG backend。**保留已有游戏 FG 路径，不主动改 swapchain/Reflex/驱动设置；#16/#63/#126 保持独立历史，不标已修，也不作为此开发批次等待条件。

## 已完成的真实继承

- #131：0.4.5-fix1 DX11 迁移、活动 R/独立 subrect、偏移回填、句柄 flags、原生回退。
- #132：真实成功 Reset 漏记修复、创建观察、F8 默认单 TXT；F7 图像仍单独。
- #134：已从其实际 merge `145689aee077fa51c54d0b1dba2d79bc38633b2c` 整合，合并提交 `cf2184cf30e20b4c11298924ec0ae658c6b9820c`。保留全部11个文件和祖先关系：强度放大颜色预算、主界面模型控件、模式下精度控件，以及相应测试。不是复制旧UI覆盖新控件。
- #133 原有前置 FLOAT/TYPELESS 设备能力适配、合法typed SRV和FP16中间链；源图无UAV不再直接拒绝。
- 固定 Bridge v1.4.12 `28aed4099b0fe1c207b20b5fee5364c0773c25c2`；当前 native Vulkan 仍未开放，不能以包含上游文件冒充已接通。
- 本轮修复统一生成器的 #134 UI/颜色头文件 pins；要求三个来源均为祖先，保证原样接入而不是遇校验失败就回滚新版UI。

BG3旧DX11 beta的维护者通过记录保留、#58保持关闭。新组合的游戏表现尚未知。2K25 #127仍需最终验证，#134没有解决其全部原因或HTGame所有后置故障。

## 精度和链路语义：后续实现不能退回旧约定

`R`是游戏真正的SR前输入，`O`是SR/RR输出，前置`N/R`、后置`Npost/O`分别定义。现有前置100%必须N=R，0关闭NR且保留精度，非零编辑不自动打开F6。当前内核可用50–100%；1–49%明确不支持，不能仅放开滑块或暗中按50%运行。旧输出相关预设在用户编辑前保留，不把保存的CustomWorkScale称作当前所有档位的实际值。目标中新装100%及更低比例需要真实迁移/内核实现；本轮不做N>R。

Model strength 与最终 Strength amplification 不混合。#134强制颜色预算不能在新provider/RR/后置路径中被跳过。降低NR工作图不等于降低游戏R；没有引擎控制器时照样允许NR，但显示真正R控制不可用。

## 有序开发队列：每项必须落到实际执行路径

### MC0 — 完整生成与可维护构建，关联 #133/#134/#47

**立即工作**：运行真实 `prepare-multi-api-beta.py`（固定上游检出）和当前源测试；检查生成后的颜色policy、精度UI、native consumer、typed SRV确实进入编译。修复生成脚本/签名/包含次序/平台编译问题，不能只让metadata通过。随后尽可能编译双语言核心、nrchain和carrier，执行WARP/Shader/native/F8。

当前几个生成器是历史演进层，不应无限继续套新的全量文本替换器。打通本组合后，可把稳定接线逐步落到受编译的模块，保留源身份和上游版本审查，不做一次性推翻主cpp的大重写。

本地Windows工具不可用或托管CI无steps：记录真实错误与精确SHA，保留可重放命令，继续MC1等可独立源码/主机工作；不要反复重跑相同空任务结束每轮。已知编译/源码错误必须修，不借“之后用户实测”跳过。

### MC1 — Feeder/无DLSS输入真正接入，关联 #4/#80/#14

**第一优先可见新增入口**：先D3D12，再复用已验证DX11 transport。阅读固定DLSS5-Feeder生产代码（不只README）和本项目 `nr_native_bridge_abi.h`、consumer/client、真实NR前后置入口。

实现一个可调用的统一处理入口/适配层，首先包住原生Feature1路径并保留行为，再让外部提供者执行同一NR核心。版本化C ABI或明确内部接口必须同时有producer和consumer，不以结构体、文档或空回调当完成。帧描述带source/provider、view/epoch/frame身份、Color/Depth/MV/Exposure、各自subresource/active rect、颜色与运动单位、jitter相位、Reset及资源/命令完成所有权。

Native/Extracted优先；没有才允许Synthetic（画面+ReShade Depth+OFA/可用估算MV）。输入质量低要显示来源/限制，不单凭“Synthetic”拒绝。缺少必需数据或传输失败才保留原图；不得用上一视图/上一来源的guide冒充本帧，来源切换要建立新的历史边界。OFA不可用可以使用明确实现的估算后端，但不假装native MV。

先证明同一源帧完成capture→NR→合成→输出回填与关闭/错误回退，不要求先控制真实R。别同时运行Feeder自带另一个NR所有者，别再安装一套抢同一NGX入口的hook。主机mock仅验证控制流；WARP可用确定性假NR检查图像回填，不能宣称其运行了NVIDIA模型。

**交付**：真实调用点、完整payload路径、提供者选择/输出lease、原图回退、同源frame提交计数、可编译测试、F8字段。至少一个实际生产路径，而非仅发新载体计数。

### MC2 — RR成功输出后置与缩放，关联 #124/#15/#12/#82

先修现有Mode1完整输出的实际后置入口，再实现D3D12 DLSSD/Feature13 **成功返回之后**的RR Output Provider。用户Mode2请求保留，RR活跃时有效路线为RR→NR；RR解除恢复原请求。手动坚持前置、关闭自动及F6关闭必须明确。不得把Feature13改成1、清RR计数、伪造Reset或调用不属于当前视图的输出。

先100%输出网格；再做O快照→匹配缩小Npost→NR一次→匹配修改恢复O，50/67/75/100%有真实资源和尺寸接线。guide必须适配到当前输出相位，不能不加分析沿用SR前jitter。保持alpha、色域/曝光、UI边界和准确fence退休；零修改/identity、关闭和失败可回到已经成功的原生RR输出。

FFX RR入口作为后续同一Output Provider适配，不把“检测到DLL”当观察到完成的RR。NR→RR不是本轮先决条件。

### MC3 — 格式适配与真实R调节，关联 #2/#94/#82/#13

格式工作贯穿MC1/2：优先补曝光R32/R16 typeless、常见Motion/Depth视图与后置可写输出。物理layout、合法typed view、device capability和数值语义分开；前置SRV可读走owned canonical，后置需实现可证明的写回（UAV/RTV或同布局编码后copy）。资源族表不是游戏白名单；不在FLOAT失败时随意换UINT，也不因optional诊断缺失锁死整轮NR。一般格式拒绝按资源/帧恢复，真设备丢失/硬故障不得吞异常继续提交。

真实RenderScale完善现有NGX optimal-settings请求、应用代次和实际资源回执；扩展provider自己的尺寸控制/引擎profile。UI显示requestedR、实际activeR、O和pending/restart/unsupported原因，不能用历史allocation单值判成功。固定DLAA的Luma/其他提供者需要真实引擎尺寸控制，不能仅改Evaluate尺寸；选定一个可落实的UE/profile路径再推广。分辨率改变时更新资源/历史，不用每帧Create/Destroy。

### MC4 — Vulkan两条入口，关联 #80/#94/#104

后处理/Synthetic入口优先复用Feeder在ReShade可控边界的共享传输；native DLSS入口另行实现正确的同帧提交依赖。保留1.4.12的输入输出格式解耦和半精度曝光，不直接把当前硬关的vk_mirror翻成1就声称完成。

必须处理未提交VkCommandBuffer里的同帧输入、队列外部同步、image layout/ownership、external memory/semaphore、copy-in→NR→copy-out顺序。上游event-park已明确记录的规范问题应替换为成立的路径；不能仅因外部Endfield成功推定所有driver成立。遇真正阻塞，保留实验记录和可用后处理入口，继续别项；不要无限以“Vulkan有风险”替代实现。

### MC5 — Legacy reuse与已提取原生输入，关联 #4/#14/#80

不从零复制全部API：DX10 relay、DX9经DXVK/dgVoodoo2、OpenGL共享transport、x86→x64 helper各做有明确版本/协议/输出所有权的adapter。优先把同一consumer在helper实际加载并完成工作，不仅增加Manager列表。跨进程不得直接传COM指针；明确位数、协议、slot与进程退出时的在途所有权。

Luma/奥德赛/Skyrim已有标准DLSS提供者先互操作，TAA结构提取只在确需新provider时独立加入。FSR/XeSS先验证外部转换器→标准输入与本core共存，再实现公开SDK adapter；不复制未审许可证的OptiScaler源码。平台/引擎profiles收集已知slot/格式/解码，而不是硬编码“游戏名即允许”。

### MC6 — 面板、配套包与最终一次测试，关联 #13/#47/#80

沿用#134主面板，补Native/Extracted/Synthetic、实际路线和R/N/O；只给已连通能力可操作控件，其他明确pending，不做假的已启用开关。Manager先提供可回退的选装backend和进程/架构检测，不并进另一条产品分支全部代码。

统一包须一个核心、唯一选中的carrier/provider、nrchain和适用helper；按API/位数配置选择而非全叠装抢入口。保留原INI，不带NVIDIA模型/运行库或游戏文件。生成source manifest和逐文件hash；F8依然单TXT。把“源完整/编译通过/CI通过/游戏待验”四个维度分开记录。

## 本轮结束/交接规则

每个开发对话从本队列第一个尚未完成且可推进的执行项开始。每轮至少完成一个有行为的源码增量或修复真实构建阻塞，提交commit并更新#133/#80；仅重复规划、改状态、加空ABI不是完成。工作块可单独短分支/PR，但归并回唯一兼容集成支线并保留祖先，不另造长期分叉。

游戏验收栏由维护者最后填写；未验收不关游戏Issue、不报净提帧或“全部兼容”，但继续开发。最终支持声明必须按真实来源/API/版本分层。MFG、N>R、Feature18原生Upscaling、0.5视觉算法不在本批。

## 最终游戏矩阵（全部是候选，不是支持承诺）

BG3 DX11升级回归；BG3/Endfield/RDR2 Vulkan；Skyrim DX11；Tarkov式typeless离线fixture（不绕反作弊）；CP2077 RR→NR；DOOM2016 Vulkan synthetic；Metro2033 Redux DX11 synthetic；Fable Anniversary x86/DX9；OpenMW或DOOM3 OpenGL。

每个样本第一轮：固定同一新包、原有运行库、R游戏控制、NR100%，F6观察+F8一个TXT；成功后分别降N和改R，不把多个变量混在一起。持续异常立即回退。由维护者统一选定实际可测游戏，不要求其每轮提供新日志解锁源码开发。

## 新对话读取顺序

最新#133与分支HEAD → 本文 → `NEXT_CHAT_PROMPT.zh-CN.md` → #80 → 当前执行项关联Issue/评论和实际源码。旧聊天SHA仅是来源，不是“最新版本”。GitHub读写优先连接器；本地编译需要时发现ROG-win-devspace/DevSpace实际可用动作再调用，不能凭旧错误认定永久不可用，也不能凭用户授权声称已经进入Windows。
