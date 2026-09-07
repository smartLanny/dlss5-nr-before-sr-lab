# Compatibility dev2: RR 后置、可缩放后置 NR 与 typed 导引读取

## 来源和当前交付状态

这是 `codex/multiapi-045-integration` / PR #133 的增量，基于已在线核对的
`f367feac9da5ebf26b0f472f94c4440565664c8d`。此次读取到的 #132 仍为
`2ab31a17388fb9cfe3c6394397df4cb555b7a172`，已经是 #133 的真实祖先；没有发现新的
远端提交，不能再创建一个假的“已合并 #132 更新”。#132 的 Reset/F8 文件均不修改。

**本交付是本地源码增量，尚未推送 GitHub。** 已发现的 GitHub `create_file` 调用返回
`Resource not found`，重新发现后可执行清单只含读取操作。ROG Windows 工具未出现在本会话。
没有新远端 commit、CI 运行或可供游戏安装的二进制。包内不含 NVIDIA/游戏/第三方模型二进制。

## 本轮新增的可执行代码（不是配置占位）

### 原生 D3D12 RR 输出后置 NR

`ProcessRrPost` 针对**真实观察到的 Feature13**，不把它标为 Feature1，也不清空 RR 所有权记录。

1. 为 RR 注册独立 feature generation；捕获其 Create 时的 depth type。
2. 使用已有的同设备命令状态、队列/资源生命周期方法准备完整输出快照、导引与私有工作纹理。
3. **恰好调用原生 RR 一次**；参数、输出指针及原生返回值不改变。
4. 原生成功后重新检查句柄身份/generation与所有权，才对该输出运行 NR。
5. NR 拒绝、普通返回失败或状态不可用时保留原生 RR 图像，不重新运行 RR。
6. 同步期间真正的所有权/生命周期不确定仍使用原有隔离。不会吞硬异常后继续写游戏资源。

用户的 `Mode=2` 保留；实际 RR 调用走 effective Mode1。原生 RR 正常释放后，普通 Feature1
恢复原请求。这里没有根据时间猜“RR 已闲置”，已有多视图/句柄隔离要求保持。

**当前 RR 范围**：原生 D3D12 Feature13，已知硬件深度（`DLSS.Use.HW.Depth=1`），满足现有
后置 Output 格式/使用方式/曝光条件。线性深度或未知类型没有伪装成设备深度：记录具体原因，保留
RR 图像。线性深度→设备深度的投影转换、FFX RR 提供者、DX11/Vulkan RR frontend 尚未实现。
CP2077 是候选，不是本版本已验证游戏。

### 后置独立工作图 Npost/O

新增 `PostWorkPercent=50..100`，默认100，UI按钮100/75/67/50，可拖动并松手提交。
`RRPostEnabled` 默认1；必须先读到 INI 或收到明确 UI 请求，才启用 RR 分支。

```
原生 SR/RR 完整输出 O
       ↓ 快照保留完整 O（包含真实 output base 的有效区域）
正确数值域的面积过滤 O → Npost
       ↓
NR(Npost → Npost)
       ↓ 同一重建核采样 Proxy 与 Neural
将模型相对改动合到完整原始 O（不整张替换为缩小画面）
       ↓
原有游戏后续处理
```

原图快照分辨率与模型纹理分离。切换物理 O 但保持相同 N 时，安全退休的轮换槽会重建完整快照和
对应描述符。输出区域偏移、原始 alpha、零效果/identity/warmup 的原生保留路径继续有效。

100% 是**新后置路径的全尺寸基准**：EncodeCS 在 N=O 仍使用原直接读取分支，Combine 无缩放时仍
使用直接模型像素。不能承诺和旧版游戏运行结果逐位相同，因为此次后置导引处理也有变化。

### 导引与颜色相位

运动矢量字段的采样尺寸（R或O）与矢量单位分开。后置缩放复用现有导引解析/重采样，但不再把
高分辨率 motion field 裁成 N；矢量位移按 NR 工作网格转换。`nr_post_compat::Motion` 是可测试的
规则表达；实际 GPU 绑定复用核心现有 ResolveNrGuideBinding/BindNrParameters，通过源转换接线。

后置颜色已被 SR/RR 重建，给 NR 的颜色抖动为0；原 SR 输入 jitter 留在已捕获的原生参数中供
诊断。**原生深度仍来自 R 网格，深度/运动与重建颜色的相位对齐尚需游戏验证**，不把零 jitter
自动称为通用时域正确。没有合成新的 depth、没有臆造反向 Z。

颜色过滤在当前声明的数值域中进行；sRGB/PQ路径在过滤前解码、过滤后编码，再走现有代理。
不加入任意整数格式重解释或自动猜色域。FP16中间精度/高光和肤色变化仍需验证。

### 修正 typed 视图重建漏口

#133 创建/绑定主 SRV 已保存 typed format，但 `WriteCodecDescriptorSet` 仍使用物理格式，
在重新生成描述符时可能退回 TYPELESS。本增量同时改写该函数的源 SRV 和运动 SRV。

运动重采样读取支持 RG16/RG32/RGBA16/RGBA32 的 FLOAT 与对应 TYPELESS，通过实际设备
ShaderLoad能力检查；FLOAT不行不转 UINT。该扩展作用于**需要重采样的路径**，并不声称所有
未经重采样、直接进入 NVIDIA 的 typeless 导引也已兼容。

### 设置/UI/采集

请求不覆盖用户 Mode；改工作尺寸使用现有配置/历史代次和 fence 退休规则。
F8/F7采集持有配置时不改请求；磁盘保存失败只作用当前进程。显式重复请求也会使已在进行的旧
INI读取失效，防止同值点击后被陈旧磁盘结果覆盖。

显示 RR 尝试次数、NR录制次数和最近阶段/时效，明确区分 requested / recorded / historical。
录制不代表已提交、GPU完成或已经显示。现有泛用状态页仍可能按用户Mode展示信息；新增RR状态区
优先用于判断该路径，不能以旧模式标签推导成功。F8维持#132的单个命名TXT，没有另造ZIP默认。

## 代码接入

唯一新增入口 `scripts/prepare-compat-dev2.py` 包装 #133 的完整事务式生成器：

```powershell
python scripts/prepare-compat-dev2.py --upstream <Bridge-1.4.12-checkout> --output build/compat-dev2
./build/compat-dev2/core/scripts/build.ps1 -ToolchainRoot <llvm-mingw> -ReShadeSourceRoot <reshade>
```

仍要求真实 #131/#132/#133 祖先、精确核心和上游blob、干净已提交源。先执行 dev1源转换，再运行
post转换；typed描述符和PE描述文字在同一临时事务里修改，最后重新计算所有源SHA256。
旧 dev1 入口和所有旧工作流不删除，用作基线。当前提交必须通过完整组合检查，不能借旧绿灯。

新工作流包括两套主机编译器、sanitizer、实际核心补丁锚点、完整中英文addon/bridge编译和所有
实际生产 HLSL compute 入口编译；只生成候选，不发布 Release。**工作流文件存在不等于已执行。**

## 本地验证边界

参见包内 `LOCAL_VALIDATION.json` 和文本记录。实际编译运行的是生产路由/尺寸策略、真实
ProcessRrPost函数的CPU API接缝、真实设置代码的CPU接缝，以及真实运动读取适配函数的CPU接缝。

面积过滤/双线性/原始alpha边界测试是独立CPU数学期望，不是执行HLSL；补丁/生成器测试使用
受控源码片段及事务fixture。完整核心文件测试在本环境明确skip，在CI指定真实完整文件后必须执行。

**未完成**：完整生成源检查、Windows SDK addon编译、SM5编译、后置GPU像素/WARP、NVIDIA
Feature18、CP2077/RR或其他游戏实测，以及帧生成节奏/帧时间。没有性能倍数保证。

## 本轮未覆盖的最大兼容需求

原生Vulkan同帧提交边界、Feeder/Synthetic消费者、DX9/DX10/OpenGL/32-bit Legacy transport、
FFX/DLSSD跨API统一RR frontend、UI的FG/MFG控制、GetDesc spoof仍未完成。旧Bridge1.4.12的
Vulkan event-park没有被直接打开。真正游戏R控制仍为引擎查询型请求，不是后置NR比例。

## 最终统一验收（不是要求本轮玩家提前测试）

最终候选完整构建通过后，集中验证：BG3旧成功路径升级回归；普通SR后置的100/75/67/50；已知HW
深度RR的NR关/开、失败保留、RR关后恢复原Mode2；读档/分辨率与物理O轮换；HDR/alpha/subrect；
F8采集锁/单TXT与typed描述符重建；NR/FG开关四组合及基础帧时间。线性RR保持原生也作为明确结果。

默认不切换游戏API，不替换NVIDIA运行库，不自动安装其他NR。旁路时应明确要求F8生成TXT并上传BUG，
图像异常再补F7。持续黑屏/崩溃停止并回退，不反复试崩或去掉同步保护。BG3旧通过记录不被重置，
任何新游戏“兼容”都必须属于这个固定构建组合。

## 一手依据

- 当前项目 #133 / #132 及 `nr_before_sr.cpp`、`nr_codec_descriptors.inl` 的在线固定提交读取。
- NVIDIA `include/nvsdk_ngx_helpers_dlssd.h`、`include/nvsdk_ngx_defs_dlssd.h`：
  RR共有输入/输出字段与 `DLSS.Use.HW.Depth` 的线性0/硬件1语义。
- 当前源码blob `c0a5acf0c9581af100c85aad5071b0ff5c91c833`；descriptor blob
  `0fe29f860fc63d891845f05f83ff33e63a32284f`。
