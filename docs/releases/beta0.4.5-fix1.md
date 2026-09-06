# beta0.4.5-fix1：闪烁、开关与 UI 集中修复测试版

这是可下载安装的 **D3D12 预发布更新包**，不是 DX11 包。保留 beta0.4.5 原版作为回退，不覆盖历史版本。

## 这次改了什么
- **闪烁修复候选（#121）**：修复轮换命令列表开始观察过晚、错过 Reset 的问题，减少由此导致的 NR 与旁路交替。仍要求每张列表真实成功 Reset；不是删除安全检查，也不保证所有模型或时域闪烁都解决。
- **星空不生效修复候选（#123）**：在严格限定的已知空状态下恢复私有 RTV/DSV 快照，不复用已被游戏回收的描述符句柄。该反馈的原附件是 0.4.4-test1，不冒称 0.4.5 实测结果。
- **开关更可靠**：F6/F7/F8 按下边沿只处理一次；鼠标总开关通过原配置事务及时提交，保留采集锁和防抖。
- **面板不再跟着状态文字跳位**：状态区固定高度，开关与滑条位置稳定；显示最近 SR / NR / 合成提交增量，区分“请求开启”和“实际执行”。这些计数不是 FPS。
- **F8 完整性修正（#47）**：TXT 如实显示 ZIP 的 complete/partial/failed；可选摘要缺失不能再写成 ZIP 完整。必需核心证据和采集完整性另列字段，仍能正常分享有用的部分报告。
- 保留 0.4.5 的强度上限 4、一键恢复推荐画质、HDR 显式试色警告、F8 ZIP+独立 TXT 等已实现功能。

## 安装与反馈
完全退出游戏，把旧同类 addon 和 `nrchain_nvngx.dll` 备份到游戏目录外。中文或英文 **二选一**，用同包 addon 与自有 nrchain DLL 更新；保留 INI、ReShade 和 NVIDIA 运行库，不要同时加载两种语言或新旧版本。

进原问题场景，用 F6 比较效果；Home 面板点击一次总开关，观察画面和控件是否还跳动。遇到安全旁路、开关无效或闪烁，请按 **F8** 提交本版的 `feedback-report.txt` 和 ZIP 到原 Issue／反馈文档；画面问题方便时补 F7。持续黑屏、卡死或崩溃立即停止并回退，不要求反复试崩或更换驱动。

每个 OTA 只有一个语言 addon、配套自有 DLL、说明、manifest 和离线采集脚本；不含默认 INI、NVIDIA 运行库、模型或首次安装全家桶。

## 验证范围与尚未完成
发布流程必须通过同一源码的完整 Windows 中英文编译、生产函数回归、SM5 编译、D3D12 WARP 正负对照、静态分析、F8 完整性和 OTA 校验，再上传并核对服务器 SHA256。新增完整性回归直接提取生产 TXT 状态表达式，覆盖可选缺失、必需缺失与采集中断的组合；原 Windows ZIP/TXT 集成测试继续执行。

**source-level / compile / CI-WARP 已验证不等于游戏实测。** 当前环境没有真实 NVIDIA 模型和目标游戏验证；HDR 观感、FG 节奏、长时稳定性与帧时间仍待原机复测。对应游戏 Issues 保持开放，不以发布代替验收。

本版没有完成 #122 NBA2K26 无 Feature1 载体、#82 NR 独立渲染精度与后置低分合成、#124 RR 自动后置、DX11、0.5 算法或 MFG 控制。RR、资源、队列及故障隔离保护不放宽。#64 等其他后置问题亦不因本次发布宣称解决。

## English
D3D12 prerelease hotfix: prospective command-list observation, strictly owned RTV/DSV replay, physical shortcut edges, stable overlay layout, recent pipeline activity, and truthful F8 ZIP partial status. Install one language with the matching owned companion; preserve INI/ReShade/NVIDIA runtimes. No target-game/NVIDIA runtime verification is claimed. Report remaining bypass/flicker with F8 TXT+ZIP; stop and roll back on persistent faults. No DX11, RR auto-post, independent NR precision, 0.5 algorithms, or MFG controls are included.
