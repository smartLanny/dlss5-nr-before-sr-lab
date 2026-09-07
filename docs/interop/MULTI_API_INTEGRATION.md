> 当前状态以 [CURRENT_STATUS.md](CURRENT_STATUS.md) 和 PR #133 最新 HEAD 为准。以下为历史设计/实施记录；不要根据旧未勾选项重复开发。本批交付测试候选后等待维护者进一步指示。

# 0.4.5 Multi-API integration candidate

## 当前入口

分支 `codex/multiapi-045-integration`，Draft #133，目标 `release/0.4.5-test`。当前继续开发顺序、范围和每项交付见 **MAX_COMPAT_CONTINUATION.md**；可直接复制的新对话指令见 **NEXT_CHAT_PROMPT.zh-CN.md**；执行看板 #80。

维护者于2026-09-07明确：最大兼容性需求持续开发，最后统一测试；**本轮暂缓MFG/新FG后端/倍率控制**。游戏尚未复测不阻止源码开发，未复测也不伪装为通过。最终合并维护线与发布仍等维护者决定。

## 已整合而非仅计划

- 双父`45ba2c91`保留#131 DX11/资源区域修正和#132成功Reset漏记修复/F8单TXT。
- 双父`cf2184cf`整合#134实际merge `145689aee077fa51c54d0b1dba2d79bc38633b2c`，保留全部11个文件：强制颜色放大预算、模型主面板、精度UI与测试。
- 原0.4.5-fix1核心不退回r3，不加入0.5画质算法。BG3旧DX11 beta维护者已通过，#58保持关闭；本次新组合未被该正例自动验证。
- 前置RGBA16/32 FLOAT/TYPELESS合法typed SRV与实际device capability，源图只读无需UAV，复用FP16 proxy/NR/合成；不是所有格式任意转换。
- Bridge v1.4.12固定`28aed4099b0fe1c207b20b5fee5364c0773c25c2`。可选模块枚举失败不独立阻止NR，明确重复桥/竞争NR及真实资源/队列保护保留。

## 尚未完成的运行路径

Feeder/Synthetic producer-consumer输出回填、RR→NR实际后置与低分处理、真实R控制的更多引擎profile、native Vulkan正确提交、Legacy DX9/10/OpenGL/x86 adapter。上游文件存在、ABI结构存在、模式文案或UI请求都不等于执行成功。MFG与GetDesc spoof未实现，MFG不列入本批。

前置N/R与后置Npost/O独立；当前100%是准确Reference，0关闭，合法50–100%，更小请求不能偷偷钳位冒充完成。新装100%迁移及低分后置留给#82实际实现。不做N>R或Feature18原生Upscaling。

## 构建与验证

```powershell
python scripts/prepare-multi-api-beta.py --upstream <bridge-v1.4.12-checkout> --output build/multi-api-beta
./build/multi-api-beta/core/scripts/build.ps1 -ToolchainRoot <llvm-mingw> -ReShadeSourceRoot <reshade>
```

生成器要求#131/#132/#134真实祖先；核对core、#134 UI/颜色依赖以及上游blob。最终manifest处理在临时目录完成，失败不暴露半成品，不改checkout。旧045脚本是固定转换库；不能用其旧pins直接替代统一入口。

本轮新增4项生产生成器配置的主机接缝测试通过，验证三来源祖先、旧UI pin替换、全部颜色依赖pin和桥→格式的实际组合次序。这不等于完整源码staging。新增工作流对实际生成后的core运行#134颜色预算/控件检查和历史负对照；原Windows addon/SDK/WARP工作流保留。

之前dev1已有格式策略/adapter主机检查；不能把旧检查或#134合并本身当作当前组合完整compile/CI/runtime通过。精确当前状态以#133检查与评论为准。没有真实NVIDIA/游戏测试结果，维护者最后统一验收。

## 安装与后续包

完整候选构建成功后才上传测试包，不创建稳定Release。一个语言核心+同源nrchain+匹配carrier，按backend/位数选择provider/helper，不能把多套NR全叠装。默认INI置于defaults/，不覆盖用户配置，不包含NVIDIA模型/运行库、游戏资产。

保留成功旧包。首次R游戏控制、N=100%，确认NR/提交/F6图像差异，再分别降N和调R。F6不会即时回滚引擎已分配R。安全旁路必须显示原因并要求F8单TXT上传BUG，图像异常按需F7；持续异常停止并回退。禁止反作弊绕过。

既有transport的stall/fence_progress/在途资源安全仍需要实现审查与最终压力验收；CPU放行等待不等于GPU完成。Vulkan不靠直接打开已知event-park规范问题路径冒充通用支持。
