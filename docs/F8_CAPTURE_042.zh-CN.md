# beta0.4.2 F8 本地反馈采集

按 F8 立即采集当前问题并显示进度。默认保留启用状态、模式和所有画质设置，不会为了采集打开 NR。显式对比在折叠选项内：原生 SR／前置／后置、强度、细节保护或 jitter。仅这些选项临时改变内存配置；完成、取消、超时和排队失败恢复原设置，不写 INI。新装或缺键默认强度 1.2、颜色请求 0.75；旧值包括 0 保留，没有 1.2 快捷预设。

每个 NR 组等待至少三秒，要求当前实例成功评估满 64 次，再预约 16 个连续成功合成帧及一个独立全图概览。默认 ROI 为原像素中心 256×256，可指定左上角；小图按实际尺寸截取。概览记录实际 stride，例如 3840×2400 使用 stride 8。概览不算连续段。帧号、实例、Feature 1 代数、配置、源／模型尺寸、格式或颜色合同改变会结束连续段，标记 partial；仅统计用 codecFlags bit 8 可变化。

每条有效记录有五个 RGBA32F 平面：原图、编码代理、NR 输出、最终合成、空间保护遥测。前四个取自同一次 Combine dispatch；代理和 NR 按实际合成公式映射到源 ROI。第五平面依次为保护权重、原因位、原始 log 残差、保护后 log 残差。DetailStability 默认 0，仅前置可用；它不是人脸识别或已验证的运动保护。MotionStability 只有请求值，实际不可用。

每帧 JSON 含完整请求／实际 ChainConfig、实际实例 tuning、jitter 原值与 NR 值、MV 比例、Depth／MV 尺寸、重置、预热、格式、codecFlags、CPU 曝光有效性和同次 GPU 解析的四个曝光值。jitter 与 MV 在原生 SR 前读取一次，与实际绑定共用。缺失／非有限数为 null，不假设单位曝光。perfEpoch 是内部统计代数，恢复校验排除它。提高默认颜色请求不会自动放宽未知 HDR 准入。

GPU 在第一次指令引用前登记 recording token；只有 token 失效且最新实际队列 fence 完成，才交给 worker。仅 fence 完成、取消、延迟 Execute 或重放都不能提前释放；重放／未知路径不得标为有效连续样本。已证明未提交的失效 token 可释放。worker 从映射流式写记录，不积累整组像素。

GPU 预算 128 MiB，CPU readback 加 worker 128 MiB，会话像素和最终 ZIP 各不超过 512 MiB，单 ZIP 文件 64 MiB。ROI 每四帧一块，修复旧 80 MiB 单体序列被打包器拒绝的问题。预算不足保留 partial 与原因。取消不会强行释放 GPU 尚在使用的资源。

ReShade 显示截图具有独立 Present 身份，只接受当前 runtime／请求的完整 postfix 后缀；保存路径先规范化，拒绝重复与晚到回调。零强度或 NR 旁路仍请求显示截图，30 秒上限后输出部分包。截图不证明同帧最终 SR；MV／Depth 像素与可验证的原生最终 SR 纹理仍为 unavailable／unknown。

目录优先 Documents/DLSS5-Feedback，失败回退系统临时目录，使用唯一会话目录。ZIP 含显示图、块、timeline.csv、capture-info.txt、可读「分析说明.txt」（并保留同内容 capture-analysis.txt）、当前与 previous NR 日志、ReShade 日志、配置、允许列表内当前进程模块 SHA-256、开始／结束状态及 manifest。模块哈希是 worker 测得的载入路径文件哈希，不是重定位内存哈希；runtime v2 另记录 sourceCommit、sourceDirty、artifactId。不上传、不收集整机文件、不自动生成进程转储。

## F8CHK004 格式

文件名：groups/G/chunk-000.f8c 至 chunk-003.f8c，以及 groups/G/overview.f8c。所有整数和浮点 little-endian，不依赖 C++ struct padding。

32 字节文件头：8 字节 ASCII F8CHK004；六个 uint32 为 schema=4、group、chunk（概览=4）、recordCount、roiTarget=16、planeCount=5。

96 字节记录头按顺序：uint32 index、valid；uint64 session、frame、instance、featureGeneration；十二个 uint32 为 configGeneration、sourceWidth、sourceHeight、modelWidth、modelHeight、roiX、roiY、width、height、stride、codecFlags、metadataBytes；uint64 pixelBytes。随后为 UTF-8 JSON、五个平面连续 RGBA float32 数据。有效记录 pixelBytes=width×height×80，无效记录为 0，仍保留身份和 metadata。GPU 曝光尾部四个 float 已搬入 JSON，不计入像素长度。

scripts/parse-f8-bundle.py 独立校验 ZIP 路径／重复／CRC、manifest 大小／SHA-256、格式边界、预算、头与 JSON 对应值和连续身份。它只报告证据完整性，不按 PSNR 或均值给画质排名。

## 自动验证与限制

validate-f8-controller.py 提取现行生产控制器、回调、readback worker 和 writer，使用真实文件／ZIP；覆盖默认不改设置、重复请求、错误 runtime／tag、规范化路径、零 NR、取消／超时／排队失败、旧会话晚到、恢复、身份断裂、预算及 3840×2160／3840×2400 概览。旧未接生产的 runtime／raw-ring 模型测试已移除。

--gpu 模式使用真实 D3D12 WARP、当前 CombineV2CS、生产 ring、真实 Execute／Signal／Reset、五平面读回、生产 writer 与 ZIP，验证 fence 不足以绕过存活 token、录制未完成时不能提前销毁。NR 平面是合成 fixture，不执行 Feature 18；这证明数据链路，不代表游戏画质、HDR 或真实模型通过。validate-f8-gpu.ps1 另覆盖延迟提交、精确 fence 和重放，独立 parser fixtures 覆盖损坏、篡改和 partial。2077 实机由用户按交付步骤验证，本轮未宣称实机通过。
