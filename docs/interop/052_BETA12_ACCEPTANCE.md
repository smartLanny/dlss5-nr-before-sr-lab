# Beta 12：可切换空间实验与 D13 对照

本批产品及双语包源码均为 `dd911da4cc422e26b3584be66bb8016193e7642a`，已完成完整统一构建、受控 Core/WARP 全 20 模式、七项真实 NGX 四帧短测、双语 Manager 解析和独立核查；中文 Core 已备份并安装到异环，代理未启动游戏。文档后续提交不改变产品或包源码身份。精确二进制、回退和证据见 [交付清单](052_BETA12_DELIVERY.json)。

维护者在 2026-09-29 明确要求：效果尚未达标的合理候选仍可进入默认关闭的实验区，由维护者切换判断。此前“未过质量门就完全不提供”的交付策略不再用于这两项空间实验；这不取消运行安全、资源归属、完成证明和关闭路径的验证要求。

## 本批范围

- 保留普通恢复、旧均衡/精细及暗部彩点保护的身份，不将旧算法改名充当新增降噪。
- 新增两个互斥空间选项：真实同源 B/Z 的有符号修改量四点引导恢复，以及局部修改量抑噪。都默认关闭，不引入历史或私有 NGX 推理。
- 本批真实空间 shader 的同帧 WARP 画质对照显示去噪收益，但在仅由 NR 新增的无噪声细线样例中，引导恢复与局部抑噪的修改积分分别只保留 25.65% / 45.57%；近零修改分别保留 18.25% / 36.71%。原 98% 细线及 95%–105% 有效修改保留门均未通过。按维护者要求仍提供默认关闭的手动选项，不升级为推荐；普通恢复仍是建议基线。详见 [空间结果](D:/ChatGPT/DLSS5/evidence/beta12-prep-20260929/SPATIAL_RESULTS.zh-CN.md)。Beta 8 的 58.85% 是旧实验独立身份，不替代本批测量。
- 新模式与私有 SR、外部历史互斥；来源或路线不支持时保留请求、回退原 Z 并给出原因。关闭状态不增加处理 pass 或资源分配；不声称新增代码完全没有 CPU/分支成本。
- 保留同像素零修改、原始 alpha、处理区域外内容、无效输入回退和精确奇数网格合同；B/Z 读写使用受完成 fence 约束的安全资源。
- 既有九组仍是原有对照，不偷偷加入新算法；新实验由独立配置/会话记录辨识。运行、提交、GPU 完成和画质有效分别记录。

## D13 对照合同

对照目标为 0.5 D13 与本批候选，分别测共同支持的单层和双层；不拿新版三至五层与旧版双层相减。D13 源码身份为 `7abd23569d0a64691caa8cd9adcff3596d6a04a9`，已找到原始中文普通基线包，并核对 Core 与配套链的历史哈希；没有将重编译产物冒充原包。工具及切换前核验已完成，同条件单层/双层实测仍由维护者安排。

固定游戏场景、分辨率、游戏 SR/RR、NR 层数、实际工作分辨率、模型及可固定的运行库；新版额外实验全部关闭作为版本基线。SR 和 RR 分开测。任何一版 NR 实际没有工作，该段不作为性能收益。两版用同一外部工具采集进程显存和帧时间，D13 内部计时缺失不以新版局部 GPU 时间补齐。

显存需归属目标 PID，区分 Dedicated/Shared/Committed 与采样范围；采集器自身的 DXGI 读数不能当游戏显存。帧间隔、GPU 工作时间和插件局部时间不能混名。静态场景、预热/计时、失败与未知保留原语义；整游戏帧新增成本 ≤3% 仍需维护者实测。

## 验证与交付

[canonical shell 收据](D:/ChatGPT/DLSS5/evidence/beta12-canonical-dd911da4-r1/shell-receipt.json) 为 passed / 实际 exit 0，生成源码清单 SHA-256 为 `7d6d833d98bf29bd401e3c7fd44d386ac14d572d83d41f3063f75645d0b86cf0`。[完整 Core/WARP 回执](D:/ChatGPT/DLSS5/evidence/beta12-private-core-dd911da4-r1/validation.json)为 20/20 模式通过，包含两种空间实验的数值、关闭、切换和生命周期检查。七项[真实 NGX 硬件短测](D:/ChatGPT/DLSS5/evidence/beta12-hardware-dd911da4-r1/validation.json)均为四帧、实际 exit 0：原有 SR/RR/私有 SR 三例与空间模式 1/2 × 原生 SR/RR 四例。它们证明受控人工输入下的创建、录制、提交与完成，不证明游戏画质、Esc、显存节省或整帧成本；四帧不足以判断稳定性能。

[双语本地包](D:/ChatGPT/DLSS5/evidence/beta12-ota-dd911da4-r1/validation.json)通过真实 Manager 解析；[独立完整核查](D:/ChatGPT/DLSS5/evidence/beta12-independent-dd911da4-r1/independent-validation-complete.json)七项通过。[异环安装回执](D:/ChatGPT/DLSS5/evidence/beta12-nte-install-dd911da4-r1/installation.json)为 passed：中文 Core SHA-256 `1eb2b2c67c7d62f4f05a239da89f41372f7d57a51e072cfcced748417a151356`，保留配置和运行库、旧 Core 回退及单一搜索位置；未启动游戏。包未在线发布、未合 main、未改 Manager 默认版本。Esc 仍待同步游戏证据；NR→私有 RR 未实现。D13 对照工具已备妥，实机显存和帧时间差尚未测得，不能宣称整帧新增成本 ≤3%。

首轮源码 `9c8bb025` 的完整构建因默认值抽取测试缺少真实空间 policy 头文件而退出 1；[原失败收据](D:/ChatGPT/DLSS5/evidence/beta12-canonical-9c8bb025-r1/shell-receipt.json)保留。`dd911da4` 补齐测试依赖后从头构建通过，该失败不混记为 GPU 执行或画质失败。额外旧配置策略探针不属于本次 canonical 链，未纳入通过项。
