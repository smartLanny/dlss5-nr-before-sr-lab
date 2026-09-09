# 快速接入无 DLSS 游戏：基于 dev.13 的实施评估

结论：沿用现有 Vulkan 接入，新增覆盖先做 **x64 DX12 Feeder 后处理 → 我们的 Core**，之后再接 x86 host。先交付完整 NR 输入/回填链，不把真正引擎 SR、FG、HDR 和所有旧 API 同时塞入首批。

## 实际已交付的起点

本轮直接静态读取 dev.13 的便携版与安装版，两包各 22 个 Vulkan 文件均符合内嵌 recipe，和 win-unpacked 一致。没有运行两份成品。

- 套装：`nr-vulkan-047-20260909`。
- Core：`0.4.7beta`，来源 `4ecc6d02ca6058cb1ebb0faae5aa49b34b0614ad`。
- Core SHA-256：`93011d9283615ea9dc8e92955f5ca6aeff01435925f63e941dc1eea1128a372c`。
- Feeder SHA-256：`3346e4a74f380f4fa189ea0e8d9f8d0accb87a415b6f3ebba84d2c5a8ff6f709`。
- recipe SHA-256：`3f57618756c63070b6c727371842412b79fbad1a501b8b5a8e0d42627ed30e4a`。
- 便携版 439,276,867 字节，SHA-256 `7fdee911b2135730171eac3abd2dea44b8cb86dec71b79a04ae1167f9a11ed41`。
- 安装版 439,688,960 字节，SHA-256 `adb4d59bc16ccc42fb244d212387e62db678f87783321acddf30fbb732a4aeb4`。

dev.10 文档里的 R5 说明已被 [dev.11 交接](C:/Users/pc1/Documents/ChatGPT/DLSS5/xiaofeng-maintenance-20260908/docs/handoffs/2026-09-09-manager-dev11.md:24) 更新；dev.13 沿用后续配套。实际 recipe 保留 `scope=controlled-renderer`、`realGameVerified=false`，不能把受控验证宣传成全部 Vulkan 游戏可用。

## 第一批：x64 DX12 无 DLSS

已有调用链：

`Feeder 采集 → 唯一 Core owner → BuildSyntheticFrame → SubmitFrameV1 → 提交命令 → 围栏确认 GPU 完成 → 回填同一帧`

现有 [patch-feeder-external-nr.py](C:/Users/pc1/Documents/ChatGPT/DLSS5/nr-vulkan-provider-20260909/scripts/patch-feeder-external-nr.py:150) 已组装颜色、深度、运动矢量、尺寸、frame/epoch 与 queue/fence；后续路径已有调用、完成等待和错误保留。它固定上游 Feeder 提交 `26c002d…`，第三方包内 0.13.1 不是可随便替换的同一成套版本。

最少剩余工作：

1. 固定一份 DX12 Provider recipe：配套 Core、nrchain、runtime、Feeder、ReShade、导引 shader 的角色/版本/哈希与来源。
2. 在真正 ReShade DX12 回调中完成颜色解释、深度/MV 查找、同一队列与同帧回填、resize/reset、退出清理验证。人工纹理直接调 Core 不替代这一步。
3. 现用安装器新增独立 `feeder-dx12` 路由，保存实际 EXE 绑定、配套身份和安装来源。沿用现有文件事务、备份、修复与卸载恢复。
4. UI 分开显示“已准备文件”“本次输入已加载”“本次 NR 已完成”。工作分辨率仅描述 NR 成本，不写成降低游戏原始渲染分辨率。

原生 DLSS 保留现有 `native-dlss` 路线和已有 DLSS 门槛；不要删除全局 `ERR_NO_DLSS` 后把所有游戏塞入原生安装器。Feeder 只用于无原生输入或明确选择的场景，同一游戏不能同时保留两套 NR owner。DX12 继续通过已有 ReShade proxy/add-on 注入；不覆盖来源未知的代理 DLL。

首批范围建议明确为 x64、RTX50、SDR、已有支持格式、单样本完整画面；不默认开启 Feeder 的 `work_upscale` 实验。

## 最小验收

- 一套带真实 Present、深度和运动的 DX12 受控场景加载实际 ReShade＋Feeder＋Core，完成连续帧、F6 对比、一次 resize 和正常退出；成功必须包含 NR 后的同帧输出回填。
- 缺 Core、重复 owner、摘要变化、围栏 Signal/等待失败时保留原图，不回填未完成结果。
- 合成游戏目录验证安装→修复→卸载，写入失败回滚，原配置保留，以及原生/Feeder 路线切换。
- 一款允许模组的离线 x64 DX12 无 DLSS 游戏完成真实回调验收，检查文字、快速转镜、遮挡及开关回退。受控测试与实机结果分别记录。

已有 Vulkan 受控矩阵不因这一项重跑；只在共享组件改变时补相关回归。

## 第二批：x86 和旧 API

复用上游 `32 位 Feeder → 64 位 host` 传输，在 host 的 D3D12 接收端调用同一 Core Provider。新增工作集中在 Core 注册、官方 NGX 初始化、共享句柄/帧参数映射、frame/epoch/fence 寿命、游戏与宿主退出清理。

不能让 x86 进程加载 addon64，不能跨进程传 COM 指针。x86 与 host 必须整套锁版本和 IPC；先确认深度/MV/颜色信息是否齐全，不把合成导引标为原生导引。随后再给安装器加独立 `x86-host` recipe 与双位数校验。

DX9 转译优先采用已有 dgVoodoo2/DXVK 适配；DX11 无 DLSS、OpenGL、HDR 和引擎级 FFX→DLSS SR 各自验收，不作为首批 DX12 Feeder 的前置条件。

## 与本次打不开问题的关系

用户报告的是 Electron 管理器在创建子进程时失败，发生在游戏注入之前。更换 NR/Bridge/Feeder 不会修复这条管理器启动链。启动故障的临时兼容入口与诊断修正见 [启动排障说明](STARTUP_SANDBOX_20260909.md)。
