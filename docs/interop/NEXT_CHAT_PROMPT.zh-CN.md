@GitHub 请继续维护 smartLanny/dlss5-nr-before-sr-lab 的最大兼容性支线，直接开发、测试并提交，不要再只做调研、规划、文档或占位接口。

唯一持续集成分支：codex/multiapi-045-integration；集成PR：#133；目标仍为0.4.5维护线release/0.4.5-test；执行看板#80。开始先在线读取最新main、目标维护分支、#133 HEAD、open PR/Issues及相关近期commit，不能拿旧聊天SHA当最新。读取分支内docs/interop/MAX_COMPAT_CONTINUATION.md，并以其有序队列推进。

#131 DX11和#132 2K25/F8已合入；#134已从实际merge145689aee077fa51c54d0b1dba2d79bc38633b2c整合，兼容线的整合commit为cf2184cf30e20b4c11298924ec0ae658c6b9820c，后续还有构建pin/接续文档提交。先核对祖先，已存在就不要重复移植。保留#134强度放大颜色预算、主面板模型参数和模式下NR精度。F8默认只生成一个命名TXT，F7单独补图。不要回滚到r3核心、旧UI或ZIP+TXT。

我的要求是尽可能完整地扩大游戏/API支持，我会最后统一测试；你不必每做一点就停下来等我逐游戏验证。没有游戏/NVIDIA环境时继续真实源码、主机、编译和WARP工作，明确标记runtime待验，不要宣称实测完成。MFG兼容、解锁、倍率UI与新FG后端本轮全部暂缓，不作为开发阻塞；不破坏游戏现有FG即可。保留BG3旧DX11 beta已由我通过的事实，但不冒称新组合或其他游戏也通过。

按顺序行动：
1. 先打通当前完整source staging/构建，入口scripts/prepare-multi-api-beta.py，Bridge固定v1.4.12实际commit28aed4099b0fe1c207b20b5fee5364c0773c25c2。确认#134 UI/颜色依赖与pins一致，生成后的源码实际进入编译。托管CI无steps或本机暂不可访问时如实记下，转下一个能推进的模块，不无限重试空任务。真实源码/编译错误必须修。
2. 立即做MC1：Feeder/无DLSS的真实producer→consumer→同一个NR Core→结果回填。先D3D12，再DX11复用transport；优先native/extracted guides，无则允许ReShade Depth+OFA/实际实现的估算MV。Native/Extracted/Synthetic来源显示清楚，低质量不等于禁止用；不能只加ABI结构体、计数器或假开关。
3. 做#124的实际RR成功输出后置，保留用户Mode2请求，RR活跃时有效RR→NR，RR关闭恢复请求；不伪造Feature1或清RR状态。先100%，再按#82/#12实现50/67/75%后置工作图与匹配修改回填；guide相位、曝光、alpha/subrect和原生输出失败回退要正确。
4. 贯穿完善动态typed view/设备能力/必要copy-convert，特别曝光/运动/深度和后置写回；完善真实R控制请求与实际activeR/O确认，再接具体引擎profile。NR比例变化不是游戏真实R变化。100%前置必须N=R，0是真关闭，未实现的小比例明确拒绝，不偷偷跑50%；本轮不做N>R。
5. 做Vulkan后处理/Synthetic transport，再处理native DLSS中途同帧提交；不是简单打开上游有已知event-park规范问题的vk_mirror就算完成。复用Feeder等实际代码的合法边界与共享同步。随后做DX10 relay、DX9 wrapper、OpenGL、x86→x64 helper的版本化adapter，别复制多个NR核心。
6. 收敛同一面板、可选backend、配套包、来源manifest和F8，按最终一次测试矩阵交付。

普通资源兼容用合法格式族、实际设备能力和必要中间纹理，不要仅因不在旧表、Synthetic质量较低或optional枚举失败就全局锁死。确实的GPU同步、资源生命周期、subrect、数值语义、原生Reset、唯一NR所有者仍要正确，不靠吞AV/伪造fence/乱改GetDesc强开。安全旁路必须明示原因并要求F8上传BUG。GetDesc spoof只在能证明后续访问合法的独立最后实验考虑，不是正常格式解法。

每轮完成有实际行为的代码或真实构建修复，运行能做的测试，提交commit并更新#133/#80和相关Issue。复用#2/#14/#124/#15/#82，不重复建同题RFC。分项可以独立短分支后并回兼容支线；禁止force-push main、移动旧tag、自动发布稳定Release。最后汇报精确branch/commit/PR、改动、实际验证级别、未完成项和下一执行入口，不以“等你测试”结束整个开发方向。

需要Windows编译时先发现并尝试ROG-win-devspace/DevSpace连接器当前可用动作；工作区C:\Users\pc1\Documents\ChatGPT\DLSS5\dlss5-nr-before-sr-lab-product。先检查工作区状态，使用独立worktree，不覆盖其他agent未提交工作。GitHub读写/PR/Issue优先连接器，不凭过去一次错误称永久只读；工具确实不可用才如实报告，并继续可执行部分。
