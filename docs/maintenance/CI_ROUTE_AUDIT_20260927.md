# #311 C.3：当前 main 的托管 CI 静态路由审计

审计日期：2026-09-27。冻结基准：`b2990b120337cdd8026f83a1bd369f3708b78fbe`。
本记录承接 #311 的 14 job 路由请求；只审阅 workflow、被调用脚本和测试的源码，
没有启动 Actions、构建或游戏，没有修改 workflow、运行时、配置或发布资产。

## 入口与证据分类

[`pull-request-validation.yml`](../../.github/workflows/pull-request-validation.yml)
仍有 **14 个顶层 job**。每项均要求 `vars.ENABLE_HOSTED_CI == 'true'`，并限定
main PR 或 `workflow_dispatch`。14 不等于实际 runner job 数：reusable workflow、
编译器和系统矩阵会展开，部分调用还重复进入同一构建。没有把跳过记为通过。

下表的“当前原始源”指 checkout 的 tracked `src/config/tests`，不是最终生成 Core。
“最终生成”指统一入口的 QualitySuccessor 链；“历史负控”指有意保留的缺陷源码，
必须在指定断言失败，不能把编译/导入失败当作缺陷重现。workflow 链接均相对本记录。

## 14 项路由表

| 顶层 job / workflow | 实际脚本、测试和源码身份 | 依赖 / 内部可选条件 | 后续行动与依据 |
|---|---|---|---|
| maintenance / [beta042-validate](../../.github/workflows/beta042-validate.yml) | bug-sweep、043r2、compat-sweep、044-compat、retirement-return、strength-cap、recommended-picture、045-color；反馈/OTA测试；beta04 structure/resize/math；codec、资源观察、capture、command-state CPU/WARP；最后 `build.ps1`、`package-045.py`。多数读取当前原始源。 | Ubuntu g++；Windows 固定 LLVM/ReShade、D3D12 WARP；完整构建依赖 host 和 shader 两项。 | 保留回归，迁移完整构建/包装。当前仍硬找 `*beta0.4.5-fix2*.addon64` 并检查 PE 0.4.5.2，与当前 `build.ps1` 的 0.5 输出冲突，见下文。 |
| feedback / [feedback-integrity](../../.github/workflows/feedback-integrity.yml) | 当前 `test_core_feedback.py`、`test_f8_parser.py`、parser 语法；固定 `4498e47edf51693d2721f8ec8563054a515c1643` parser 的五项缺陷负控，另核对 blob `2934c61e5c1417fe58b99b43701dc15121466363`。 | Ubuntu/Windows × Python 3.11/3.13；额外历史 checkout。 | 保留历史负控和当前 parser；与 followup 的重复调用合并。 |
| baseline-and-release-contracts / [followup-validate](../../.github/workflows/followup-validate.yml) | 再调用 beta042、feedback；独有 `validate-feedback-report-status.py` 正/负控及 `publish-045-fix1.py` 语法检查。状态 oracle 读取当前 `nr_feedback_report.inl`。 | Ubuntu；嵌套旧维护完整构建和 parser 矩阵。 | 拆分独有回归后合并重复调度；旧 publisher 语法检查只能标历史工具维护，不代表当前发行验证。 |
| compatibility / [compat-continuation](../../.github/workflows/compat-continuation.yml) | `compat-post-dev2.yml` 的最终生成候选；另 `git archive HEAD src scripts tests config third_party .github docs/interop` 保存私有源码输入。 | 完整历史、明确 PR head；双编译器 host → Windows；源码附件保留 7 天。 | 保留候选和私有输入证据，与 multi-api/research 共用一次候选构建。tracked 目录 archive 不等于公开导出 allowlist。 |
| exposure / [exposure-access](../../.github/workflows/exposure-access.yml) | `tests/multi_api/exposure_access.cpp`、`test_exposure*.py`；当前实际 adapter、片段和完整原始源 patch tests；`MULTIAPI_CORE_SOURCE=src/nr_before_sr.cpp`。 | Ubuntu g++/clang++；clang ASan/UBSan。 | 保留 FLOAT exposure 访问/格式回归；可归并便携测试组。 |
| single-text / [f8-single-text-validate](../../.github/workflows/f8-single-text-validate.yml) | `single_text_policy_test.cpp`、validate-command-creation/reset-notification-gap；固定 `61fe9a0fe527fb2304c382c332cbe62876228b8b:src/nr_command_state.inl` missed-Reset 负控。 | Ubuntu 双编译器、完整历史；默认 checkout，没有显式 PR head ref。 | 保留真实 Reset 负控；与 multi-api portable 重复，统一一次执行和证据 SHA。 |
| multi-api / [multi-api-foundation](../../.github/workflows/multi-api-foundation.yml) | format_access/adapter；native_bridge history/forward/render_scale/mirror_rect；multi_api unittest；single-text/reset；lockperf/hotpath/Perf4–9。当前原始源正控、历史 Reset 负控；随后委托 compat-post 最终构建。 | 双编译器、完整历史、sanitizers；检查固定祖先 `f88adc33…`、`2ab31a17…`、`145689ae…`；Windows 依赖 portable。 | 保留当前生命周期、格式、桥接消费者覆盖；合并重复完整候选，不按 Perf/D21 名称删除。 |
| generated-picture / [multi-api-picture-regression](../../.github/workflows/multi-api-picture-regression.yml) | picture-budget 正/负控、`test_picture_integration.py`；实际 `prepare-multi-api-beta.py`，在基础生成树再跑 picture-budget。 | 双编译器、完整历史、祖先 `145689ae…`；固定 bridge `28aed409…`。 | 保留基础生成回归，补最终 QualitySuccessor 的对应覆盖；本 job 本身未走最终颜色/重建 overlay。 |
| picture-controls / [picture-ui-budget-validate](../../.github/workflows/picture-ui-budget-validate.yml) | picture-budget、045-color、color-policy、bug-sweep、strength-cap、recommended-picture，读取当前原始源；可再调用 beta042/feedback。 | Ubuntu 双编译器；内部 full-build/feedback 仅手动 dispatch 或旧 base `fix/f8-single-txt-reset-evidence`，main PR 不走这两项。 | 保留 CPU 检查并去重；迁移旧 base 条件/构建分支前保存覆盖映射。 |
| recommended-picture / [recommended-picture-validate](../../.github/workflows/recommended-picture-validate.yml) | validate-recommended-picture 正/负控、validate-colour-defaults；当前 runtime/UI/INI。脚本可接受生成 root，但此 job 用原始 root。 | Ubuntu 默认 g++。 | 保留当前推荐重置与独立 INI 默认；统一候选另对生成 root 检查 colour-defaults，不能按名字视作旧默认。 |
| retirement-ownership / [retirement-return-validate](../../.github/workflows/retirement-return-validate.yml) | 抽取当前 `DestroyContext`；固定 `647ac292433bf00506eead16ccda1a30c2b3a153` 旧源 `--expect-rejection`。 | Ubuntu g++、额外历史 checkout。 | 保留：失败 Release 后保留依赖、隔离阻止 teardown 的实际所有权 oracle。 |
| strength-cap / [strength-cap-validate](../../.github/workflows/strength-cap-validate.yml) | 当前 runtime/UI/config 最终强度检查；同 `647ac292…` 旧源负控。 | Ubuntu g++、额外历史 checkout。 | 保留强度与 UI/config 消费者覆盖；可归并便携组，保留指定失败 oracle。 |
| research-050 / [beta050-validate](../../.github/workflows/beta050-validate.yml) | 050-stability、045-color、044-compat、retirement、recommended、layered-composite、strength、beta05-boost、bug/compat/反馈；structure/resize/math/shader。当前原始源；随后 multi-api → compat-post。 | Ubuntu host、Windows shader；两者完成后嵌套完整候选。 | 提取独有稳定性/boost/分层覆盖，取消重复完整候选调度。 |
| d21-uniform-chain / [d21-uniform-validate](../../.github/workflows/d21-uniform-validate.yml) | uniform-layers、present-color-source、source-frame、energy gate、color-policy 正/负控、d13 UI compile、picture integration；uniform/full policy/runtime CPU 与 `nr_full_spatial_warp.cpp` + 当前 spatial D3D12 实现。 | Windows 固定 LLVM、Python 3.12、WARP、30 分钟。 | 保留当前多层、源帧和调度消费者；energy gate 不证明实际功耗，WARP 模型替身不证明 Feature18/游戏。 |

## 最终生成与依赖

实际最终候选链为：

`compat-post-dev2.yml` → `build-unified-candidate.ps1` →
`build-perf12-candidate.ps1 -QualitySuccessor` → `build-multi-api-candidate.ps1`
→ `prepare-colour4-edge.py`。最后的 generator 使用基础
`prepare-multi-api-beta.py`，并加入最终颜色/重建等 overlay。

[`compat-post-dev2.yml`](../../.github/workflows/compat-post-dev2.yml) 固定 bridge
`28aed4099b0fe1c207b20b5fee5364c0773c25c2`、ReShade
`4a50d1eddace85734871d91792ff214f13f66c01`、DirectXMath
`28929ac6f3c1649a7641741b689ebecccb17aa8d`、LLVM 20260826、ORT 1.23.2、
YuNet 来源/许可证/动态模型哈希、FidelityFX SDK
`c6efa6bf7f2027b3ec94f28578bb5965eabb9e55` 及实际编译缓存。
FFX 准备和额外 temporal 检查在 workflow 中以模块文件存在为条件；当前模块存在，
且统一构建脚本强制 SDK/cache，不能通过关闭后端绕过。完整入口包含双语、人脸、
累计性能、最终生成 Core、WARP、重建和时域正负控，但不含真实游戏验收。

## 已确认的问题与后续行动

1. **旧包装身份与当前构建冲突。** beta042 调用当前 `scripts/build.ps1`，后者输出
   `beta0.5-dline21-uniform3`；`src/nr_before_sr.rc` 为 0.5.1。
   workflow 却要求恰好两个 `beta0.4.5-fix2` addon，并验证 0.4.5.2 PE；
   `scripts/package-045.py` 也固定 0.4.5-fix2。此调用路径不能作为当前候选包装入口。
   后续迁移独有测试至统一入口，保留历史工具身份；不是简单重命名旧包。
2. **重复调度。** maintenance 与 followup 都调用 beta042；feedback 同理。
   compatibility、multi-api、research-050 都会进入 compat-post 完整候选。
   部分复用 workflow 的 concurrency group 使用相同 `github.ref` 且
   `cancel-in-progress:true`，重复调用存在相互取消风险；本次未运行复现。
   后续先指定一个完整候选 owner，再保留各组独有前置/负控。
3. **原始源、基础生成和最终生成不能混报。** generated-picture 实际只验证基础
   generator 的 picture-budget；统一最终入口使用 QualitySuccessor。
   部分 checkout 明确 PR head，部分为默认 PR merge ref；应统一或明确两类证据 SHA。
4. **独立手动入口不受顶层变量约束。** 多数子 workflow 仍有自身
   `workflow_dispatch`，没有重复检查 ENABLE_HOSTED_CI。直接 dispatch 子入口可跳过
   顶层开关。后续与仓库“一个可选入口”约定对齐；本轮没有启动任何入口。
5. **LLVM 下载校验不一致。** beta042 与 d21 校验固定 LLVM ZIP SHA256；
   compat-post 下载同版本后直接解压，该步骤未见对应 digest 检查。
   ORT/YuNet 已有哈希检查。后续统一下载证据，不放宽现有固定依赖门禁。

## 保留历史负控的理由

feedback 的历史 parser、single-text 的 missed-Reset，以及 retirement/strength 的
历史实现分别证明测试能够捕获指定旧缺陷。它们不是把旧版本当当前候选。
脚本读取当前消费者的事实也说明 `0.4`、`D21`、`d13` 等名称本身不能作为删除依据。
退役对象优先是重复调用层和不匹配的包装路径；任何测试迁移先列出正控、负控与
最终生成消费者的继承位置，不能仅以“统一构建已存在”推断每个 oracle 都被覆盖。

## 审计局限与完成状态

本轮闭合 14 条路由的静态事实审计，不代表 CI 已修复或测试通过。
没有验证仓库实时变量值、托管环境下载可达性/调试层、外部 SDK 实际构建、
concurrency 取消行为，也没有重新证明固定历史提交的祖先关系。
除明确的版本/文件名冲突外，不预测具体 job 红绿；未启动、跳过、失败与通过分列。
后续 workflow 修改与运行验收另行执行，付费托管 CI 保持未开启的既定边界。
