# Beta13 新机开发接续

产品源码为 `cd8ba7009d333f7b03c463dc4237a913f9913081`，分支 `feat/309-post-private-sr`。Beta13 相对已验证的 Beta12 仅修改界面说明和版本身份：功能说明改短，详情悬浮；RR 状态和计数继续直接显示。NR 算法、默认开关、设置和公开 DLL ABI 没有在这一版改变。

先按根目录 [START_HERE.md](../../START_HERE.md) 从 GitHub 获取代码和私有草稿附件。源码已实际从 GitHub 全新检出并通过版本/UI 检查；开发附件已在另一个本机路径恢复并由实际构建入口消费。这不等于已在另一台物理电脑完整构建。

## 附件用途

| 文件 | 用途 |
|---|---|
| `DLSS5-0.5.2-beta.13-RTX40-50.zip` | 给 RTX 40/50 用户的 Core + Kio 1.0.6 NR 运行库，默认中文，英文 Core 单独封装供替换 |
| `DLSS5-0.5.2-beta.13-RTX20-30.zip` | 同版 Core + plain FP16 NR 运行库，面向 RTX 20/30 |
| `PRIVATE-DLSS5-DevInputs-20260929-r2.zip` | 已固定的 FFX 对象、nrchain 复用输入、完整人脸构建输入、模型与许可、恢复工具 |
| `PRIVATE-Beta13-DebugAndReports-20260929.zip` | 本地已准备，私有上传待确认；包含当前 Core 调试映射和详细报告，暂不属于 GitHub 已可下载附件 |
| `SHA256SUMS.txt` | 已上传附件的 SHA-256；先核验再解压 |

两个用户包内的 `install` 是需要安装的文件；不包含用户 INI 或游戏原生 SR/RR 库。保留一个 `nr-before-sr*.addon64`，不删除其他 ReShade 插件。英文 Core 在内层 ZIP 中，不会与中文 Core 同时被扫描。两种 NR 都是已确认的第三方变体，不能把 PE 的 NVIDIA 版本号或 SHA 校验称为官方有效数字签名。原作者说明随相应包保留。

`PRIVATE-*` 仅用于维护者开发接续，不是公开发布内容。开发输入已上传；详细调试报告因自动审批要求明确许可而暂留本地，拿到该包前不要声称看过其中材料。历史报告的旧盘符、PID 和时间只属于旧次实验；没有随附原图/转储的结论，接续 Agent 不得声称已独立查看原件。Git 历史中既有的私有反馈不构成向公共仓库迁移的授权。

## Windows 构建输入

需要 Windows x64、Git、Python 3、PowerShell 7、固定 LLVM-MinGW。硬件测试另需兼容 NVIDIA 显卡和原生游戏运行库；没有硬件时只记录软件检查，不把跳过记作通过。Windows 图形调试层为可选项：已有入口提供 `-AllowMissingDebugLayer`，并非关闭其他验证。

固定身份见 [DEPENDENCIES.json](DEPENDENCIES.json)。先把以下源码完整检出到自选依赖目录，不修改其工作树：

| 依赖 | 来源 | 固定提交/版本 |
|---|---|---|
| 上游载体 | [NIGos/dlss5-bridge](https://github.com/NIGos/dlss5-bridge) | `28aed4099b0fe1c207b20b5fee5364c0773c25c2` |
| ReShade | [crosire/reshade](https://github.com/crosire/reshade) | `4a50d1eddace85734871d91792ff214f13f66c01` |
| DirectXMath | [microsoft/DirectXMath](https://github.com/microsoft/DirectXMath) | `28929ac6f3c1649a7641741b689ebecccb17aa8d` |
| AMD FidelityFX SDK | [GPUOpen SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) | `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`，1.1.4 |
| LLVM-MinGW | [官方 20260826 Release](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260826) | UCRT x86_64；编译器 SHA `bf58cf0e544a308660645aad1fe65ae9cbc44b0871e4010278f3d9551f86a690` |
| ONNX Runtime | [官方 v1.23.2](https://github.com/microsoft/onnxruntime/releases/tag/v1.23.2) | Windows x64 CPU；所需输入已附在私有开发包 |
| YuNet | [OpenCV Zoo 固定版本](https://github.com/opencv/opencv_zoo/tree/47534e27/models/face_detection_yunet) | 原始与处理后模型及许可已附；重新准备时使用 `onnx==1.19.1` |

没有通用“装最新版依赖”步骤：构建入口校验固定提交和哈希。可用 `git clone <url> <目录>` 后 `git checkout --detach <固定提交>`；如果自行通过 Git 获取模型/SDK 二进制，确认 Git LFS 已取得实际文件而非指针。MinHook 源码已跟踪在本仓库。

以下变量仅为新机示例，可自行选择路径。`$repo` 是本仓库，`$inputs` 是已核验并解压的 r2 开发附件根目录，`$deps` 是新机依赖目录。恢复工具拒绝覆盖已存在的目标。

```powershell
$repo = (Get-Location).Path
$downloads = 'C:\dev\dlss5-handoff'
$inputs = 'C:\dev\dlss5-handoff\DevInputs'
$deps = 'C:\dev\dlss5-deps'
$cxx = "$deps\llvm-mingw-20260826-ucrt-x86_64\bin\x86_64-w64-mingw32-clang++.exe"

$archive = Join-Path $downloads 'PRIVATE-DLSS5-DevInputs-20260929-r2.zip'
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne `
    'ba1a309703c2bbb3cc7aea4c5a50a1ebce93dd123381dd52c14ca1af5ebd7211') {
  throw '开发附件校验失败，请重新下载。'
}
if (Test-Path -LiteralPath $inputs) { throw '请选择尚不存在的 DevInputs 解压目录。' }
New-Item -ItemType Directory -Path $deps -Force | Out-Null
Expand-Archive -LiteralPath $archive -DestinationPath $inputs

python "$inputs\restore-ffx-cache.py" --source-cache "$inputs\ffx\cache" `
  --output "$deps\ffx-cache-verified" --sdk "$deps\FidelityFX-SDK-v1.1.4" --compiler $cxx
python "$inputs\restore-face-inputs.py" --source $inputs `
  --runtime-output "$deps\onnxruntime-win-x64-1.23.2" --models-output "$deps\face-models"

& "$repo\scripts\build-unified-candidate.ps1" `
  -UpstreamRoot "$deps\dlss5-bridge" `
  -ToolchainRoot "$deps\llvm-mingw-20260826-ucrt-x86_64" `
  -ReShadeSourceRoot "$deps\reshade" -DirectXMathRoot "$deps\DirectXMath" `
  -FaceRuntimeRoot "$deps\onnxruntime-win-x64-1.23.2" `
  -NrchainReuseRoot "$inputs\nrchain-reuse" `
  -FfxSdkRoot "$deps\FidelityFX-SDK-v1.1.4" -FfxCacheRoot "$deps\ffx-cache-verified" `
  -OutputRoot 'C:\dev\dlss5-build\candidate-001' -AllowMissingDebugLayer
```

输出目录必须不存在，源码须干净。不要给已生成的旧 Core 改名冒充新版本。`CodecCacheRoot` 只是加速项，上述新机命令省略它，生成所需对象。

FFX 恢复只迁移八个预编译对象：原始 `build.json` 原样保存为 `build.original.json`，派生回执只改对象路径，另记 `relocation.json`。它核对固定 SDK、干净状态、编译器、许可证和八个对象，不伪装成新机从头编译 FFX。开发附件还保留修改后的 backend 文件、shader 与原始编译命令；完整从源码重建这份 SDK 缓存的自动脚本仍可后续完善。

`nrchain-reuse` 只有在当前桥源码及官方 SDK 输入与回执逐项相同才允许复用。修改桥源码后，改用 [NVIDIA/DLSS](https://github.com/NVIDIA/DLSS/tree/a291cc7d2cc642a51566f3dfd5376f635cd1b284) 固定提交 `a291cc7d2cc642a51566f3dfd5376f635cd1b284`，安装 Visual Studio C++ 构建工具，设置 `DLSS5_NGX_SDK_ROOT` 和 `DLSS5_VCVARS_PATH`，省略复用参数执行官方 SDK 冷构建。不能篡改回执来跳过同源检查。

## 验证与继续开发

本次确切结果以 [052_BETA13_DELIVERY.json](../interop/052_BETA13_DELIVERY.json) 为准。CPU、WARP、实际 NGX、Manager 解析与游戏验收各自记录。20/30 运行库在本机 RTX 5090D 的接口测试不等于 RTX 20/30 显卡已实测。Beta12 的实机四窗是 RR→后置 NR，对照没有证明省显存；Beta13 没有重新宣称那些窗口来自自己。

后续工作优先读 [已知问题](BETA13_KNOWN_ISSUES.zh-CN.md)。先区分未执行、已录制、已提交、已采用、画质有效和成本合格，再选择修复。别通过放宽预算、取消 fence、强行采用错误历史或关闭模块绕过失败。实验算法可以保留多个手动选项，默认关闭时不增加 GPU 分配与处理。功能说明使用短句，细节悬浮；状态和计数可直接显示。

不自动启动游戏、不合 main、不发布公开 OTA、不改 Manager 默认版本；需要实机时由维护者手动设置 SR/RR 并安排测试，异环优先，2077 后续。现有已失败/跳过的历史回执保留原身份，不由新构建追认。
