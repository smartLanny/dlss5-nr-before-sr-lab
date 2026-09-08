# Manager 发布前问题单与工作树核对

核对基点：Manager `8fa5b3d` / 0.4.6-hotfix.7-dev.6；远端核心 main `e1f5885`。用户明确继续等待任务 `01a080d0-f712-7f02-93b0-09cfc581d16f` 一起交付，优先做好 #191 的旧版升级、链路识别、冲突检查和备份恢复。不得由“希望尽快测试”推导提前公开发布或自动删除证据。

## 问题单分类

| Issue | 已有实现 | 仍需处理 / 验收 |
| --- | --- | --- |
| #179 现用 SR/FG | 当前 UI、逐域保存/恢复、RTX40 组件、RTX50 原生、EXE/API 选择 | RTX40/50 实际游戏采用设置、升级/卸载、长时验证。不能用界面夹具代替。 |
| #180 CPU/IO | 去重/缓存/worker/封面索引/静默日志及普通 Content 裁剪 | 原客人版本、发生阶段与 CPU/Procmon 现场仍缺；保留开放。 |
| #191 外部组件及换链路 | 完整外部目录来源、路径记忆、清单/哈希验证、受管文件回滚与可读失败；本轮补上 EXE 绑定与按记录恢复 | 更新检测应比较“已安装链路 vs 目标链路”；任意手工/Opti 旧代理与 bak 迁移未完成。本次发现的重复部署/旧 D3D12 残留 P1 已修，最终候选验证见维护交接。 |
| #129 GPU 自动 K/M | 历史自动策略保留；现用统一 K/L/M 手动选择与旧设置迁移 | 旧独立自动控件目标已由后续用户需求替代；真实效果并入 #179，不能重新恢复旧 UI。 |
| #193 风格按钮 | Manager 已是同行三个按钮 | 余项属于 Core overlay。 |
| #196 通俗文案 | Manager 已调整常用名称、状态与操作说明 | Core overlay 全量文案单独跟进。 |
| #163 / #165 / #167 | 总体 Core、provider、Vulkan 与注入式 FG 里程碑 | 不是当前 Manager 已支持的所有路线。Vulkan 仍未完成 layer/注册恢复/transport；不能写“已兼容”。 |

## #191 升级验收矩阵

| 场景 | 当前证据 / 下一步 |
| --- | --- |
| 已知 0.4.5 / 0.4.6 / hotfix.1 受管安装升级、修复、卸载 | 现有真实载荷临时目录回归覆盖 API 切换、保留显式 INI、旧 carrier 退役、原文件恢复与中途失败回滚。 |
| DX12 目标 | 保留核心选择和共用 nrchain，不部署 DX11 carrier；不能仅因 ZIP 中有 carrier 就部署。 |
| DX11 目标 | 要求配套核心、nrchain、carrier 及元数据；单独 Addon 不得配旧桥接凑数。 |
| 当前 EXE 与旧安装收据不同 | 已新增共享门禁；卸载按原收据逐行恢复。历史双目录、原 EXE 缺失、外部改动和路由歧义均有定向回归。 |
| 受管文件、备份或 UAL 被外部修改 | 现有回归拒绝覆盖，保留现场与恢复记录。 |
| 无收据手工旧链路、Opti 代理、任意 `.bak` | 尚无通用自动迁移。需要可证明的来源、文件身份及显式迁移方案；不能仅凭扩展名删除/恢复，也不能宣称已完整收口。 |
| 新 Core-only 验收包 | 当前 dev10 是 D3D12 Core-only，明确不是 Manager OTA；没有 DX11 carrier，不能作为通用升级包导入。已修复误报 DX11 元数据问题，提供可读包类型说明并继续拒绝导入。 |
| 新运行时加载方式 | 待正式包、文件职责和加载合同交付后，做旧→新→失败回滚→旧的实际文件布局验证。外部安装来源本身不代表运行时外置注入。 |
| Vulkan | 当前明确不支持；不能借用 DX11 carrier 放行。 |

## 工作树收敛

Manager 当前链已经包含 #178 → #181 → #189 → #195；`94489177`、`063a4281` 均为 `8fa5b3d` 的祖先，无需再次 cherry-pick。统一从当前维护候选接续，旧源码不再重复开发。

Manager 与核心 main 的共同祖先为 `6def46b`，但两者根目录布局不同。Manager 差异包含删除核心 `src/nr_before_sr.cpp`、`src/nrchain_nvngx.cpp` 和 `scripts/build.ps1`。原样 merge 不是正确整合方式；进入核心主线应另做独立子目录迁移，保留两边构建/测试/许可和提交来源。

以下以工作树目录名标识，`.devspace` 项位于宿主的 DevSpace 目录；“归档候选”指先保存可重建源码、产物摘要和必要证据，尚未执行搬移或删除。

| 工作树 | 状态 / 建议 |
| --- | --- |
| xiaofeng-maintenance-20260908 | 当前候选，必须保留。 |
| nr-manager-api-routing-20260908、nr-manager-api-routing-verify | HEAD 已包含、tracked/untracked 干净；可列后续归档。 |
| nr-manager-carrier-filter-20260908、nr-manager-dxgi-options-20260908、nr-manager-readiness-20260908 | HEAD 已包含、无未跟踪工作；可列后续归档，保留所需打包/验收记录。 |
| xiaofeng-sr-fg-20260908、xiaofeng-bridge-ui-20260908 | 当前 node_modules 联接依赖链经过这两棵树。先让当前候选具备独立依赖，再归档旧树。 |
| .devspace/worktrees/...-d47dbce3 | #189 已包含，保留成品/交接至候选验收后再归档。 |
| nr-manager-dx11-update | 有未跟踪 test-output 和备份测试现场；保留这些证据。 |
| manager-product-xiaofeng-work | 产品基础与依赖目标，保留回退参照。 |
| manager-launch-controls-20260908（独立 manager 仓） | #23 已关闭冻结；保留归档源码，不能把旧 src/ui 再复制进现用 UI。 |
| manager-pr21-reviewed-20260908（独立 manager 仓） | #21 仍为 Draft；有效服务逻辑已选择性迁入，保留来源和验证记录。 |
| dlss5-manager-product（独立 manager 仓） | 本地分支领先远端 3 提交，有未跟踪 distribution-input / 发行材料，必须先保全。 |

清理工作树不是当前发布门禁。旧源码已在祖先链中，但 ignored 的载荷、回退包和 junction 依赖不能按 git clean 状态视为可删。核心/0.5/provider 的活动工作树不属于本次 Manager 清理。

GitHub Actions 当前有因付款/额度问题未启动的任务；必须记录为 CI 未启动，不能反复重试，也不算通过。已通过的本地/成品检查与真实游戏验收分别记录在维护交接中。
