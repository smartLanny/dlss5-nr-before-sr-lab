# beta0.4.5-fix2：F8 单 TXT、Reset 泄漏与画质预算测试版

这是可下载安装的 **D3D12 预发布更新包**，不含 DX11。保留 beta0.4.5 原版与 beta0.4.5-fix1 作为回退，不覆盖历史版本。

## 这次改了什么
- **F8 改为单份命名 TXT（#132 / #47）**：按一次 F8，或点击“生成反馈 TXT（F8，推荐）”，本次交付目录只写一个 UTF-8 TXT；不再默认生成 ZIP、summary 临时摘要或图像。命名为 `YYMMDD-game-actual-build-HHMMSSmmm-pid-serial.txt`。正文同时保存当地时间与 UTC；“复制完整报告”和“复制 TXT 路径”指向同一成功写出的文本。F7 仍是用户主动选择的高级图像补充，F8 文本成功不冒充 F7 ZIP 配对。
- **真实 Reset 泄漏修复（#132 / #127）**：完整原生观察已覆盖同设备/同接口实现时，一张已登记但尚无 shadow 的冷列表若缺失 ReShade 前置 Reset 通知，原生 Reset 成功回调可能被忽略。本版在实际成功返回后、游戏继续录制之前，复用已有观察能力并记录该次真实 Reset。失败 Reset、身份不匹配、未覆盖实现和关闭时的冷列表均不借此放行；不新装 Hook，不复制别的列表绑定，不伪造 Reset。
- **NBA2K25 证据范围**：用户现有 NBA2K25 / RTX5070 / e5ce121 反馈中，SR 有增长、NR/合成 0、运行库未尝试、持续 `awaiting-native-reset`。这是该游戏的相关候选，不是已确认全部根因，也不是 NBA2K26 #122 或 NBA2K27 #68。#127 保持开放。
- **>1x 颜色放大预算（#134）**：在实际 codec 常量与诊断发布前强制 `coefficient=min(requested_effective,1)/amplification`。Mode 2 把着色器色度指数保持在未放大预算，亮度放大不变；Mode 1 在放大后降低颜色 MIX，不是逐像素色相/色域保证。普通 UI 无退出开关；0..1x 行为不变。
- **模型控件移到主界面（#134）**：最终强度改名为“强度放大 / Strength amplification”，放到高级画质。模型强度、局部影调/结构、脸部/皮肤和 UI 保护移入主画质区；仍走原采集锁与配置世代提交。
- **有限前置精度滑条（#134）**：精度控件放在处理模式正下方；明确 100% 选择 Reference 以实现精确 N=R。有效自定义值映射到当前 50..100% 前置核心。**1–49% 被明确拒绝，不是静默钳制。** 0 可关 NR 并保留精度；调整非零精度不会隐式打开 Enable/F6。后置低分执行未做，后置控件保持禁用并给出说明，不提供假滑条。

## 安装与反馈
完全退出游戏，把旧同类 addon 和 `nrchain_nvngx.dll` 备份到游戏目录外。中文或英文 **二选一**，用同包 addon 与自有 nrchain DLL 更新；保留 INI、ReShade 和 NVIDIA 运行库，不要同时加载两种语言或新旧版本。

进原问题场景，用 F6 比较效果。NBA2K25 请进入原旁路场景对比一次；HTGame / 画质预算请确认主界面模型控件、高级强度放大，以及 1–49% 精度被拒绝。遇到安全旁路、开关无效或闪烁，请按 **F8** 提交本版的那一份命名 TXT 到原 Issue／反馈文档；画面问题方便时补 F7。持续黑屏、卡死或崩溃立即停止并回退到 **beta0.4.5 或 beta0.4.5-fix1**，不要求反复试崩或更换驱动。

每个 OTA 只有一个语言 addon、配套自有 DLL、说明、manifest 和离线采集脚本；不含默认 INI、NVIDIA 运行库、模型或首次安装全家桶。

## 验证范围与尚未完成
发布流程应通过同一源码的结构/身份校验、主机侧生产策略回归、OTA 字节校验，以及能在本机运行的 Windows 编译。未实际执行的 Shader/WARP/游戏层不能算通过。

**source-level / compile / host 已验证不等于游戏实测。** 当前环境没有真实 NVIDIA 模型和目标游戏验证；HDR 观感、FG 节奏、长时稳定性与帧时间仍待原机复测。对应游戏 Issues 保持开放，不以发布代替验收。

本版没有完成 #53 异环后置与版本相关回归、#82 完整 0..100% 内核范围 / 新装默认迁移 / 真实后置低分合成、#121 全模型或时域闪烁、#122 NBA2K26 无 Feature1 载体、#124 RR 自动后置、#127 的全部 NBA2K25 根因、DX11、0.5 算法或 MFG 控制。RR、资源、队列及故障隔离保护不放宽。#64 等其他后置问题亦不因本次发布宣称解决。

## English
D3D12 prerelease hotfix on #132+#134: F8 now delivers one named TXT (no default ZIP), records a real Reset that previously leaked when ReShade's pre-reset notice was missing, and uses NBA2K25 `awaiting-native-reset` evidence without claiming a complete root cause. #134 adds a mandatory >1x colour-amplification budget, moves model controls to the primary picture UI, and exposes a limited pre-SR precision slider that refuses 1–49% instead of silently clamping. Reduced-resolution post-SR is not implemented. Install one language with the matching owned companion; preserve INI/ReShade/NVIDIA runtimes. Roll back to beta0.4.5 or beta0.4.5-fix1 on persistent faults. No target-game/NVIDIA runtime verification is claimed. #53/#82/#121/#122/#124/#127 and other unfinished issues stay open.
