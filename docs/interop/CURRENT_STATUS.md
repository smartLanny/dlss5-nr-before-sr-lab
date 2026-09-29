# 当前主线与交付入口

**当前本地安装候选：0.5.2 Beta 12。** 产品及双语包源码 `dd911da4cc422e26b3584be66bb8016193e7642a`；完整 canonical 实际 exit 0、生成 Core 20/20 WARP 模式、七项真实 NGX 四帧短测、双语 Manager 解析及独立核查通过。6sol 已在异环退出后备份、安装中文 Core，保留配置/运行库和回退；代理未启动游戏。两个新空间实验默认关闭，普通恢复仍建议作为基线；实际 shader 有去噪收益，但 NR 新增细线修改保留仅 25.65% / 45.57%，近零修改 18.25% / 36.71%，质量门失败，按维护者要求仍保留手动选项。见 [验收](052_BETA12_ACCEPTANCE.md)、[身份/安装/回退](052_BETA12_DELIVERY.json)、[手测单](052_BETA12_TEST_CHECKLIST.zh-CN.md)。产品身份不随此后的文档提交变化。

游戏画质、Esc 展开短暂丢 NR、显存优化和整帧成本仍待实机验证；真实 NGX 人工输入宿主不能替代游戏。D13 对照工具已备妥，尚无同条件显存/帧时间差，不能宣称整帧新增成本 ≤3%。原生 RR→NR 已交付，NR→私有 RR 未实现；本地包未在线发布、未合 main、未改 Manager 默认 pin。

**历史 Beta 11、Beta 10 状态保留原记录身份。** Beta 11 产品源码 `1329c116f0108621d342214c466f6f41c1a305bd` 的 canonical、18 模式 Core/WARP、双语包和独立包字节核查通过；其当时的硬件及安装因游戏运行未执行，不能由 Beta 12 结果追认。Beta 10 曾在异环安装，现已由 Beta 12 取代；旧包与失败记录保留。见 [Beta 11 验收](052_BETA11_ACCEPTANCE.md)、[Beta 10 交付](052_BETA10_DELIVERY.json)。

Beta 10 当轮补齐独立后续 NR 层的低提交余量准入；两种原始浮点色格式切换时保留同为 FP16 的模型存储，继续历史 Reset、slot/归属/fence 检查。真实不透明失败仍保留隔离。Esc 展开动画中 NR 短暂消失、菜单展开后恢复尚未实机归因，不宣称全部修复。

Beta 9 新进程的 SR/RR 九组各五组得到 600 个有效局部 GPU 样本；三组时域仍未采用。图片审核仍建议普通均衡，私有 SR 无普遍优势。这条“暂不必重复九组”仅是 Beta 10 反馈后的历史建议；当前按 Beta 12 测试单分别记录 SR/RR 九组；两项新空间实验另做手动对照。随后补齐来源证明，再考虑深度拒绝及短历史。局部时间不是整帧成本。

Beta 9 的 #318 RR 绑定、#319 驻留回收/有界管线缓存及真实路线报告保留。Beta 8 闪退和 Beta 9 后续层漏口均保留历史失败证据；旧包/回退不删除。仅本地更新，未推送、合 main、发布在线 OTA 或改 Manager 默认版本。

## 上一候选保留：Beta 8 与游戏反馈

**历史安装候选：0.5.2 Beta 8（#313 本地维护者候选），现由 Beta 9 取代，旧包和 Beta 7 回退保留；代理未启动游戏。** 编译/包装源码 9febbc684c0c28ad7aa55b12dd53670b64f121b8。真实 SR/RR 入口计时、时域来源诊断和首帧登记接线已修正；统一双语、CPU/WARP、实际 Feature18/Feature1/RR 及现有连续帧功能检查通过。中英文包通过实际 Manager 解析，设置和运行库不变。

Beta 8 当时的新四点空间方案降低合成噪声，但细线 NR 修改量保留未过门，该批停止且未装入产品；Beta 12 将不同的真实 B/Z 两种方案按手动实验交付，质量门仍失败。保留现有实验对照，建议普通均衡。私有 SR 普遍优势、全面时域去噪/拖影和整帧新增成本 ≤3% 仍待维护者实测；后置 RR 原生 MV 继续拒绝。

入口：[Beta 8 验收与失败记录](052_BETA8_ACCEPTANCE.md)、[身份/部署/回退](052_BETA8_DELIVERY.json)、[九组手测](052_BETA8_MANUAL_CHECKLIST.md)、[相对 D21 中文](052_BETA8_CHANGELOG.zh-CN.md)、[English](052_BETA8_CHANGELOG.en.md)。唯一路线沿用 [Beta 6 评估及后续](052_BETA6_EVALUATION_AND_NEXT.md)。本地提交不代表远端已更新；未推送、合 main、发布在线 OTA 或改 Manager 默认版本。

## 上一候选保留：Beta 7 与后续游戏反证

**历史安装候选：0.5.2 Beta 7（#313 本地候选），已由 GPT-6 Sol 备份并部署异环；代理未启动游戏，维护者随后已录制真实游戏反馈。** 编译及包装源码均为 `80dcc99952b9cdd277d9780b292a2a169a5ff83f`。完整双语统一构建、严格 CPU/WARP、最终生成 Core 的真实 Feature18/Feature1、25 组后置 RR、672 帧连续时域和 4K 产品链路通过；中英文 Core-only 包通过实际 Manager 解析器。

私有 SR 的同输入受控暗部、文字和发丝较 Beta 6 旧实现改善：发丝误差 0.007088→0.005700，最差帧连续段 12→95/100 像素，零修改区域漏色为零。同档普通恢复仍有部分细线/发丝优势；降噪和拖影没有全面达标，新增整游戏帧成本 ≤3% 尚未证明。实验保持默认关闭，原有设置和运行库逐字节保留。

入口：[Beta 7 验收与限制](052_BETA7_ACCEPTANCE.md)、[精确身份/部署/回退](052_BETA7_DELIVERY.json)、[一键手测](052_BETA7_MANUAL_CHECKLIST.md)、[相对 0.5 D21 中文更新](052_BETA7_CHANGELOG.zh-CN.md)、[English](052_BETA7_CHANGELOG.en.md)。唯一路线继续使用 [Beta 6 评估及后续路线](052_BETA6_EVALUATION_AND_NEXT.md)。源码与文档本地完成；远端 #313 更新状态以实际推送结果为准，不将本地提交当作远端已更新。

游戏由维护者安排，先异环、2077 最后，原生 SR/RR 分开设置和记录；保留 Esc/切镜、Present、地图省电和多层暗部验收。同 Core ReShade 专项仅为 0 层 NR 私有 Feature1，真实多层 Feature18 在原生接口独立验证。私有 Feature13 RR、跨原生 RR 修改量及 NVOF 不列入本轮通过项；未合 main、未在线发布 OTA、未改 Manager 默认版本。

## 上一候选保留：Beta 6 游戏证据与回退

Beta 6 编译源码 `e7407a39d12a6d43bf4e557f8c041e8361296831`。维护者完成异环 18 张截图及设置恢复，本次未重现 Beta 5 私有创建闪退；当时私有 SR 画质未过关，性能没有有效同条件窗口，三个时域组未通过采用，其中 AMDOF 定位为 4K 预算误拒。Beta 7 受控验证不改写这份游戏反证。

保留 [Beta 6 验收](052_BETA6_ACCEPTANCE.md)、[身份及原回退](052_BETA6_DELIVERY.json)、[游戏评估](052_BETA6_EVALUATION_AND_NEXT.md)和旧包。Beta 5 九张图、崩溃及 heap-only 中间方案仍保留于 [故障记录](052_BETA5_NTE_FAILURE.md)。

## 故障候选保留：Beta 5 一键截图与可选本地 OTA（#313）

Beta 5 编译源码 `de8d5ab851e1f635e4bdb14b60fe8a5cc8aa84c7` 已完成完整统一双语构建、本批实际 Core 控制器与 GPU/NGX 验证。**实验页一键比较九组，每组最多三张，预热并核对实际采用，完成/取消恢复原设置；提供本地双图对比页。** 游戏自身 SR/RR/HDR/FG 不自动切换；截图不用于性能计时，也不是同帧 B/Z 像素配对。

最终策略 18、控制器 86、WARP 17 模式、显卡四模式、真实 SR 两层25%奇数尺寸及 RR-only 五层50%通过；42 份私有 SR/时域相关源文件与 Beta4 不变，但重编对象不全相同。Beta4 完整矩阵保留原身份，本轮短测不能证明稳定低开销或整帧 ≤3%。详细边界见[Beta5 验证](052_BETA5_ACCEPTANCE.md)。

中英文 Core-only OTA 已通过当前 Manager 原始包读取器验证（未操作 Manager UI）。包只包含 Core、配套 nrchain、许可与说明，**未在线发布、未改默认 pin、未合 main**。异环由 6sol 仅部署和准备 Beta4 回退；已有 INI、NR 库及原生 SR/RR 保持原字节，游戏由维护者验收。全部精确身份和部署回执见[交付清单](052_BETA5_DELIVERY.json)。

入口：[一键对比/手测](052_BETA5_MANUAL_CHECKLIST.md)、[相对 D21 中文更新](052_BETA5_CHANGELOG.zh-CN.md)、[English changelog](052_BETA5_CHANGELOG.en.md)。旧候选和故障证据全部保留，下面 Beta4 为历史冻结记录。

## 上一候选保留：统一 Beta 4（#313，历史快照）

源码候选 `85b59bdc89a23db14f06d911e83a26a385af351c` 已冻结，**r6 canonical 构建 exit 0，软件回归及受控真实 GPU/NGX 功能验证已完成**；受控成本已测但稳定低开销/整帧 ≤3% 未获证实，游戏验收待完成，异环已部署并完成回退核对。已纳入 Beta 1 安全修复、Beta 2 菜单、#316，以及运动/历史、原生 HDR/jitter 私有 SR 和集中验收接线。未合 main、未发布 OTA。

最终 r6 证据：实际 Core 私有 WARP/显卡各 17 模式、集中验收控制器 77 项、真实 NR/私有 SR 9 场（1–5 层 50%、25% odd/100%、RR-only、RR+5 层）通过；Present Adaptive、Low Frequency 和两套时域关闭运行库基线各 9 场，总计 36/36。私有 SR 成本三场各 32 帧通过；4K 时域成本八场功能通过但时间强多峰，详见[原始成本说明](D:/ChatGPT/DLSS5/evidence/beta4-final-temporal-cost-4k-r1/COST_RESULT.md)。三组光流模型实验 r6 复验通过，仍属独立研究。RR 最终矩阵使用异环 310.5 运行库；旧 310.1 小尺寸挂起及 HD 通过记录继续作为历史限制。受控人工输入不能代替游戏降噪或整帧 ≤3% 性能证明。

Present 合法内容源跳号曾被 AMD 误拒，生产修正已纳入并由 r6 原生对象的原始 `+3` 来源夹具复验：两种时域模式各 9 场都有实际历史采用。旧失败与混合对象单场只保留诊断身份。产品功能、准入和证据统一见[本批矩阵与证据](052_BETA4_UNIFIED_ACCEPTANCE.md)。

游戏继续由维护者安排，先异环、2077 后；FG 先关闭。新功能对新安装默认关闭，已有保存设置包括旧 jitter 均保留，不要求重置 INI；不合 main、不发 OTA、不改 Manager pin。Manager 当前为[运行库/OTA 合同交接](052_BETA4_RUNTIME_CONTRACT.json)，并非应用改造已完成。

最终身份以 [r6 候选 JSON](D:/ChatGPT/DLSS5/deliveries/052-beta4-unified-85b59bdc-20260927-r6/0.5.2-BETA.4-CANDIDATE.json) 与[独立交付清单](D:/ChatGPT/DLSS5/deliveries/052-beta4-acceptance-20260927/DELIVERY.json) 为准：`source-manifest.json` SHA256 `ee983859d3e529d4aed93bba6ff855af0d7b99f8a8c3c166ca09a1f33691c1de`；中文 Core/ZIP `061561fe76e758bded01ff85080e42768151a15017ce3442da04b5f57ec2db9a` / `8f2da829298346ca44144351db5f806d98beaf7c527cd68edab4c1b9d847ebfa`；英文 Core/ZIP `30ec1d00c4f4e4f916524db606fa0afc87016d91536e85e6b4700d73638abd77` / `4d725724daf6d73b9719bb32d85ec07df5e1cd071f9264188a8fd5cb877e5d2d`。**稳定低开销与游戏验收仍待完成。** 异环已由 6sol 部署中文 Core 与 Kio 40/50 NR 库；[安装回执](D:/ChatGPT/DLSS5/evidence/052-beta4-nte-install-20260927/installation-receipt.json)确认唯一 Core、配置原字节和原生 SR/RR 不变，未启动游戏。[回退包](D:/ChatGPT/DLSS5/evidence/052-beta4-nte-install-20260927/rollback.zip)及[使用说明](D:/ChatGPT/DLSS5/evidence/052-beta4-nte-install-20260927/README-rollback.md)位于搜索目录外，回退脚本 VerifyOnly 已通过。 以下公开候选与历史回执保留原身份。

## 唯一主线和执行责任

#300 已合入 `main`，归位提交为 `134269606fc2bc8028604a8766d96d815726696d`。
完整 #306 / #307 已包含，0.4 冻结；#310 归档、#312 全新/浅克隆检查随后合入。
#308 的 Beta 1 仍待游戏短测、未合 main；#313 当前本地安装候选为 Beta 12，产品源码 `dd911da4cc422e26b3584be66bb8016193e7642a`，游戏验收仍待维护者；Beta 11 的未完成硬件/安装状态与 Beta 10 历史安装身份保留；Beta 5 异环验收失败，原本地 OTA 暂停交付，未在线发布；Beta 3/4 保留历史身份。
维护者已要求本执行 Agent 继续收尾和后续推进，不再等待 Claude/其他会话额度。
新任务从实时 main 开短期分支，不重新恢复多套 D16/D21“最新集成线”。

**验收更正：UI1 仅初步开启看起来有效，维护者尚未完整验证。** 两项新功能默认关闭；
保留 Inbox1 回退，不因源码整理而强制更新 Manager 组件 pin 或游戏配置。

## 当前可获取的 Core

### 已公开可选候选：0.5.2 Beta 1（#308，未合 main）

| 项目 | 精确身份与边界 |
|---|---|
| 编译 / 包装 | `c382bd19fb9a759e57319feb4ea09b9c0caa9962` / `9c63c29b8f47826bcdaa44991e00d4fbec2256b0` |
| 中文 Core SHA256 | `2b0a6a36843b8dc4f7cc0dfb6c10af6e917270215f95a1588a0a4ed333858da7` |
| 中文 ZIP SHA256 | `ec9b83e550e3030ff368b8bd0a3bbb12ad14bcab2a8f133be03fdc1603d27e09` |
| 下载 | [Core 0.5.2 Beta 1 预发行附件](https://github.com/smartLanny/DLSS5-Manager-ZJZ/releases/tag/core-0.5.2-beta.1) |
| 变化 | 空工作尺寸与换算后 MV 数值检查；可选 guideDomain 回执；没有更换颜色/NR 模型算法 |
| 验证 | 完整 Windows 双语、CPU/累计、WARP/硬件 shader 及人工静态输入的受控真实 Feature18；游戏短测仍缺 |
| 不包含 | #313 的私有残差 SR 后端、私有 RR、新的黑点/FPS 改进承诺 |

#308 的 `13d1cca7` 只更新说明，未换包、改 Manager pin、游戏或 INI；保留 UI1/Inbox1 回退，main 运行时仍是 UI1。

### 已集成基线及回退：0.5.1 UI1 / Inbox1

| 项目 | 身份和范围 |
|---|---|
| 公开测试包 | 0.5.1-Beta-Reconstruction1-UI1；主线维护后的同运行时重编译 |
| 本次公开包编译 SHA | `deba69db3fc8ec652d6a0a01a848a74340ed54b6` |
| 本次公开包文档/包装 SHA | `4351ca3b117abfb10abc8b55d47ab47828c08f17` |
| 中文 Core SHA256 | `1778b3c105e0d7a7eaa6f58f53cb5980423de528a946e54b7cea10bd04d61f59` |
| 回退 | 0.5.1-Beta-Inbox1；不是已全面游戏验收的稳定版 |
| 下载入口 | [Manager 仓库的 Core 预发行附件](https://github.com/smartLanny/DLSS5-Manager-ZJZ/releases/tag/core-0.5.1-ui1-preview-20260926) |
| 交付说明 | [UI1](051_RECONSTRUCTION1_20260925.md)、[Inbox1](051_INBOX_WINDOWS_20260924.md) |

公开附件中的 `CORE-ARTIFACTS.json` 对应上述编译/包装身份和全部哈希。
原 UI1 的 `31cddd07` 编译 / `21db404d` 包装仍按历史身份保存，不与本次文件混用。
维护收尾不改变产品版本；`0.5.2-beta.1` 已用于 #308，后续候选按独立验收递增，不重标已有包。

## 已在代码中的能力

完整 unified10/Inbox1/重建UI1、#303缓存和累计性能/退休保护；一至五层独立
模型参数与历史，整链工作比例只应用一次。颜色范围、光影控制、已有前置、
后置及成功 RR 后置路线、人脸后端和受限线性 HDR AMDOF 均保留。

“低分辨率细节增强”是独立开关，下有均衡/精细并排按钮；“去除暗噪”独立。
二者默认关闭，不联动 AI 色彩、模型、比例、层数或 FG。去暗噪在最终采用结果
处稳定极暗候选在高恢复增益下的不可信色度，保留已选目标亮度；不是逐层
时域降噪，不保证消除模型自身全部纯黑亮度斑块。请求、录制、提交与画面分开。

main 尚无私有 residual SR；#313 Beta 7 完成私有 SR 最小修正、独立采样和后置原生 RR 矩阵，证据见页首；私有 Feature13 RR 未实现。空间恢复或 AMDOF 不等于私有 SR。
NVOF 和部分原生 MV/历史作用域仍不可用，以实际准入和对应验证为准。

## 验证范围（以下首两项专指 UI1 维护构建，不替代 Beta 1 回执）

- UI1 维护构建完成 Windows 双语、完整人脸、46 组验证、287 个生成文件核验及累计/扩展 Core、重建数学/UI/WARP 正反例。
- 该次 WARP 用受控模型数据，未重跑真实 Feature18、游戏或硬件。原 UI1 的 5090D 回执仍属于原构建。
- 维护者仍需补彩色光照、黑点、有效 NR 细节、低比例/多层性能及游戏兼容验收。
- Windows 为主；GitHub 只有 `ENABLE_HOSTED_CI=true` 才执行托管入口，目前未付费。
  跳过、失败、取消、未启动与通过分别记录；持续旁路须 F8 反馈，不能算提速。

## 历史保全与已整理入口

[归档索引](../maintenance/ARCHIVE_INDEX_20260926.json) 保存 177 个远端旧名、36 个原工作树 HEAD、222 个本地历史别名和 3 个原仅 reflog 可达提交。
[恢复说明](../maintenance/ARCHIVE_CLOSEOUT_20260926.md) 与 [实测回执](../maintenance/ARCHIVE_RESULT_20260926.json) 记录 34 个目录共 78,795 文件、57.52 GB 逻辑字节，经实际恢复校验后移除，压缩约 2.13 GB。
完整私有文件、Git 状态和恢复清单在 F 盘；保存不等于已经集成。原始录制、共享依赖和发布原包未删。
#310 的工作树/空间数量是历史快照；新增任务会改变数量，当前值看 #294/#311，不重新发布为实时清理成绩。
维护者在 #311 接受 F 盘单份已验证归档，不再要求 NAS 第二份；不声称跨设备冗余，不重试已拒绝的 Z 路径。
Perf7/Perf9 测试目录链接及其 C 盘目标继续保留，不跟随链接删除。
Manager #15 只补公开附件和验收说明；Manager 另行推荐 0.5.1 不代表本批修改 pin，更不是全面游戏验收。

## 活跃任务

| 入口 | 下一步和不可丢的经验 |
|---|---|
| #294 | 本执行 Agent 的维护/研究总索引，记录归档及实际完成状态 |
| #274 / #302 | 同源输入/原始NR/恢复/最终SR定位黑点，保住色彩和有效结构；独立脸区识别另查 |
| #277 / #298 | 同进程同工作量分析等待、提交、创建与总VRAM，不混旧ETL和局部账目 |
| #293 / #296 | 首次创建与切镜恢复分开，不删必要Reset/fence |
| #308 | 已解决冲突并完成 Beta 1 软件/受控模型验收、公开可选 OTA；只差真实游戏短测，不提前合 main |
| #309 / #313 | 当前 Beta 12 产品源码 `dd911da4` 已完成统一构建、20 模式 Core/WARP、七项真实 NGX 四帧、双语 Manager 解析及异环安装；Esc、真实游戏显存/画质/整帧 ≤3% 均待验收。两项空间实验默认关闭且未过质量门；NR→私有 RR 未交付，未合 main、未在线发布 OTA |
| #211 / #239 / #275 | NVFP4/NVOF等按实际实现迁移；50系运行库约5%反馈独立同Core验证 |
| #196 / Manager | 完整菜单仍有后续；Core附件与Manager应用、安装与运行责任分清 |

私有 SR 优先做后置低分 NR 最终修改量的独立重建，与相同工作量空间恢复比较。
保留零修改、小正负量化尾差和有限越界分类；全链只重建一次，不丢第一层贡献，
不在每层重复私有 SR，也不默认堆叠多套长历史。跨原生 RR 为后续独立研究；私有 Feature13 RR 已移出近期候选。

## 已完成协作与历史执行记录（不替代页首 Beta 6 状态）

[Beta 3 接线与验证](PRIVATE_SR_CORE_052_BETA3.md)：编译/包装源码 `4f5ce993c6341e4649496cc618d105ef77a7d538`。
实际Core通过1 Create/5 private Evaluate/6 submit、准确Reset退休和零修改字节对照；WARP/显卡通过尺寸、残差及生命周期反例。首版仅已知原始零jitter，非零拒绝；游戏仍待测。
[维护 PR #316](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/316) 已推送并同步 #308/#311：14 job 静态审计已完成。
2077 三历史版本在当前环境同创建阶段故障，未发现混版证据，根因未定；全部游戏实测暂停，由维护者稍后统一安排。
Beta 1、已构建 UI 候选与 UI1/Inbox1 均保留；Beta 3 默认关闭，未合 main、未发布 OTA、不改 Manager pin。
[#311 执行清单](../maintenance/311_EXECUTION_PROGRESS_20260926.md) 区分完成、待游戏验收与开发任务；协助请求不等于已接单。
9/26 历史实验：实现 `c0eb2af2`、文档 `6fb01d73` 的 CPU46、WARP/显卡各319、受控 Feature1 691 项通过；当时未接 Core，不覆盖当前 Beta 3 验证。
#312 已补全新 checkout 的 build 目录处理和浅历史拒绝，不应重复修。

维护依据：[本地验收接续](../maintenance/LOCAL_ACCEPTANCE_FOLLOWUP_20260927.md)、[CI 路由审计](../maintenance/CI_ROUTE_AUDIT_20260927.md)。旧静态接线及 Beta 3 产品回执保留其时间身份；Beta4 保留历史矩阵，当前 Beta6 以页首对应回执为准。

## 历史

旧状态、规则、README、工作流和清理提案在[历史目录](../history/pre-mainline-20260926/)；旧交付与实验保留原路径。
历史默认、旧未接通状态及已纠正的验收描述，不覆盖上面的当前说明。
