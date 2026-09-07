# 统一候选的可重放Windows验证

从PR #133对应的已提交干净源码建立独立worktree。已有worktree、游戏目录和未提交草稿不能用作清理目标。只使用统一入口，直接运行核心build.ps1不会生成兼容carrier接线。

固定依赖：Bridge `28aed4099b0fe1c207b20b5fee5364c0773c25c2`、ReShade `4a50d1eddace85734871d91792ff214f13f66c01`（含子模块）、DirectXMath `28929ac6f3c1649a7641741b689ebecccb17aa8d`、LLVM-MinGW `20260826-ucrt-x86_64`。依赖目录只读复用；构建器核验其Git身份及干净状态。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-multi-api-candidate.ps1 `
  -UpstreamRoot '<固定Bridge检出>' -ToolchainRoot '<LLVM-MinGW根目录>' `
  -ReShadeSourceRoot '<固定ReShade检出>' -DirectXMathRoot '<固定DirectXMath检出>' `
  -OutputRoot '<全新候选构建目录>' *> '<本轮构建日志路径>'
if ($LASTEXITCODE) { throw 'Candidate validation failed; read the actual log' }

powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-multi-api-candidate.ps1 `
  -BuildRoot '<上述候选构建目录>' -OutputRoot '<全新交付目录>'
if ($LASTEXITCODE) { throw 'Exact-source package verification failed' }
```

构建/打包之间不要修改源码或文档、移动HEAD。build和package拒绝已有输出目录；修复后使用新目录保留旧日志。`validation.json`记录实际源码commit、依赖、编译器、通过项及二进制SHA256；`source-manifest.json`记录生成源。packager核对这些证据与当前HEAD、全部源码/二进制/INI和后置WARP输出，分别生成中英文包。

门禁覆盖实际生成源、格式/曝光/运动能力、NGX ABI正负控、设置/采集、RR Release三入口54场景及旧泄漏负控、生成后的颜色默认/模型恢复、F8及carrier日志、原生命令状态/栈/WARP、48场景后置production shader、静态分析和双语/core/bridge/carrier构建。WARP的神经平面为测试替身，不等于NVIDIA模型或游戏通过。

最终staging还执行carrier完成/退役生产函数35项状态断言、真实WARP fence完成/超时和Create/Evaluate硬件异常子进程测试。负向blend像素测试必须以指定错误退出；PowerShell5使用独立进程日志捕获，避免预期stderr被误当脚本自身异常。OTA不附带INI，保留旧配置；包内UPDATE-NOTES.md是玩家更新说明。

旧严格`tests/post_sr_snapshot_gpu.cpp`需要D3D12 Debug Layer。本机缺该组件时返回`0x887a002d`，单独记为未通过，不移除要求、不用其他WARP结果替它背书。实际候选已有独立48场景后置WARP证据，两者范围不同。

托管CI因账户付款/额度问题未启动时，以annotation记录真实失败原因；本地门禁明确标为local，不能把源码测试通过写成GitHub CI或游戏通过。
