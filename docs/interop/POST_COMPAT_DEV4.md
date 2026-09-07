# beta0.4.5-multiapi-dev4 统一测试版

这是合并颜色/UI fix、D3D12/DX11兼容增强、RR后置及低分后置的实验候选。不是稳定版，不包含ReShade、NVIDIA运行库/模型或游戏文件。精确源码、验证范围和文件哈希见随包manifest、validation.json、SHA256.json。

## 安装和回退

退出游戏，先备份现用同类addon、配套nrchain_nvngx.dll和INI。解压中文或英文包之一，将该包的一种语言核心、同源nrchain_nvngx.dll和同名dev4 carrier放到现有ReShade加载addon的位置。不要同时留旧lab核心/carrier，也不要叠加Feeder或通用RenoDX NR所有者；与本项目无关的MOD保持原样。

保留原来的nr_before_sr.ini。本OTA不附带替换INI，已有设置不会因解压而被覆盖。要撤销以前保存的模型强度2，用主界面“仅恢复模型强度1.0”；一键推荐会同时恢复其他画质项。

使用已有正常工作的ReShade与显卡对应NVIDIA运行库。此包不自动切游戏API、不改驱动、不提供一键卸载。需要回退时退出游戏，只移走本次三个组件，放回之前备份；不要删除其他MOD。

## 第一轮测试

先在同一原场景保持100% NR工作尺寸，用F6比较开关，再切前/后置看效果。记录有效/无效、颜色/肤色变化、闪烁和崩溃；按F8生成一个命名TXT。画面异常方便时再F7。已有资料无需重交。

| 路径 | 随后重点测试 |
|---|---|
| D3D12原生DLSS | 前置与后置；后置100→75→67→50%；读档/切分辨率后画面边界、HUD和颜色 |
| D3D12光线重构 | CP2077等RR打开时看实际后置记录；关RR后是否恢复原请求；线性/未知深度被拒绝时原图应保留 |
| DX11已有DLSS | BG3旧成功路径升级回归；其他原生/模组DLSS游戏按各自已有输入测试 |
| 异环 | 原开场场景、前后置与F6；F8新增blendObserved/blend四值用来解释拒绝，不代表本版保证已修 |
| NBA2K25 | 原问题场景确认SR/NR计数及Reset证据，未通过则继续保留Issue |

NR工作比例只调整NR成本，不等于降低游戏真实渲染尺寸R。先稳定一个变量再调另一个。前置100%=R，0关闭NR并保留精度，非零编辑不自动开启F6。后置请求比例和真实NR尺寸分开观察。

DX11桥接会等待真实GPU完成后才回填，可能降低帧率；本版不承诺性能提升。一次等待超过500毫秒或发生提交故障后，该桥接会停止，保留原生画面和仍在使用的资源。请按F8记录后重启或回退，不要把原生回退当作NR已生效。

## 如何读状态

历史NR尺寸可能早于当前设置。RR句柄存在、请求开启、已录制和已提交均不等于GPU完成或画面已显示。F8保留这些不同证据，并增加同锁采样的真实blend四值；unobserved/unavailable不是四个零。requested.blend仍是用户强度，不能当D3D12四值。

性能日志`A1-RR-owner-observed`仅表示存在RR所有者，不能单靠这一标签判断NR执行或旁路；请结合RR-post、Feature18及提交记录。该名称修正继承自dev3。

本批没有开放无原生DLSS的Feeder/provider-only、Vulkan、DX9/DX10/OpenGL/x86 Legacy、线性深度/FFX RR或新FG/MFG路线。BG3旧版通过不等于新组合所有游戏通过。

## 本地验证与剩余范围

完整生成源、双语核心、nrchain与carrier实际Windows构建，以及格式/ABI/后置状态和48场景生产shader WARP已运行，最终结果以本包validation.json为准。WARP使用确定性神经测试替身，不是NVIDIA模型。线上CI若被账户额度阻止，不能标为CI通过。

原生导引与重建颜色的相位、NVIDIA Feature18实际运行、HDR/肤色/运动表现和性能由本轮游戏测试判断。严格旧post-SR snapshot harness仍需D3D12 Debug Layer；本机无该组件，不能把它记为通过。持续异常时保留F8并回退原包，无需反复试崩。

反馈回原游戏Issue；总入口为仓库main、Issue #80及docs/interop/CURRENT_STATUS.md。本批交付后等待维护者测试和下一步开发指示。
