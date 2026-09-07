# 2026-09-07 main统一与0.4.5-DX11-兼容增强维护

维护者明确授权将最新版本收敛到main，改善F8反馈与稳定性。当前工作基线7b9ebb8普通合并c03e3ab（dev3/fix2全部祖先）与原main 4e11c35；main独有50提交、52个bug-inbox文件完整保留。

## 分支处理

main是唯一持续开发入口。历史源SHA及处理计划见BRANCH_SNAPSHOT_20260907.json；仅当分支当前SHA仍匹配快照、且完整属于验证后的main祖先时才删除已整合临时远端分支。现有本地worktree、分支及未提交文件保留。旧release基础分支暂保留以免影响未完成PR，但不再独立积累维护修复。

0.5分层算法、Manager、Feeder和其他未合入实验保留；本轮不会把未完成路线按版本号混入生产兼容核心。PR #133/#137/#131及其他已包含维护PR由main实际祖先关系收口。

## 版本与证据边界

本轮候选0.4.5-DX11-兼容增强，核心PE 0.4.5.104，carrier使用独立0.4.5-DX11-兼容增强文件名。dev3 Release/Tag b03f474保持原字节。dev3游戏测试仅作为基线，0.4.5-DX11-兼容增强不继承实机通过结论。构建/打包使用干净固定提交，自动证据为source-manifest.json、validation.json和SHA256.json。

稳定性修复c8fc8f9：采集请求在首次Present前取消时原来可能永远保持Busy；现在原子撤销未领取请求并公布失败状态。图像采集取消与Present/Watchdog共用控制锁，防止取消落在检查请求与公布session之间。独立真实Windows核心采集136检查和图像控制211检查通过；最终组合完整Windows门禁已通过，见下方记录。

反馈精简02fb888已合入：每源24KiB、F8总量192KiB；32MiB前缀与64KiB最新尾部，单来源400ms协作预算（在同步16KiB读取之间检查，尾部预留100ms，不是可抢占的硬IO超时）。保留首个不同故障和最新状态，明确重复次数/截断/缺失；数字错误码不作为计数归并。读取边界与超长首错保留已有回归。父任务补充约束：只有纯时间戳格式前缀能压缩，状态字段前缀保留；尾部中途结束也标截断。

12份抽样旧摘要约60–165KB变为11–13KB，所有样本reason集合保留；其中8份是已保存摘要再精简，4份dev3原始日志则比较新旧算法。不是所有可能日志的无损保证，详见COMPACT_FEEDBACK_BENCHMARK.json。最终组合验证已完成，见下方记录。GitHub CI和本地门禁分开报告；不因源码合并关闭仍未验收的游戏Issue。

补充安装器反馈已按实际Manager历史与11项针对性回归核对，见[安装器问题分流](INSTALLER_FEEDBACK_TRIAGE_20260907.md)。报告版本不确定，不据文件名判断新旧回归；未复制用户身份/聊天原文。

公开名称按维护者要求为 **0.4.5-DX11-兼容增强**；机器构建身份 `beta0.4.5-dx11-compat`，PE `0.4.5.104`。安装器替换旧0.4.5选择项，保留历史源身份与Release字节；显示名/安装器本体版本/实际组件版本须分开识别。

## 最终核心与OTA验证

二进制源码固定 `dccb5b398ccc540723f84a6ef789dd70dbda5cd3`；精简打包源码 `c7c133e5478ba0a64c5891fc5258a9c14cc630d6`。后续文档、CI和新增bug-inbox归档提交不改变已编译src/config/build inputs；已独立比较为空。包内build-info明确区分两个SHA，不把文档HEAD冒充二进制来源。

- Windows完整统一构建 `dx11-compat-pass3` 退出0：multi_api 67、native_bridge 16、反馈策略310、TXT策略28、真实Windows核心采集151、F8图像控制211检查通过。
- 48场景后置production shader WARP、24场景命令状态WARP和坏blend负控通过；RR释放54场景及旧泄漏负控、颜色/推荐/格式/ABI等门禁通过。
- 英文/中文核心与nrchain实际LLVM静态分析零诊断；双语核心、nrchain和carrier均编译链接成功。交付仅保留中文核心；保留上游7条novtable属性警告记录，不宣称零编译警告。
- 最终精简packager的4项实际PowerShell回归通过，含中文文件名/安装说明、完整性篡改拒绝、缺门禁/来源身份拒绝与单中文核心。
- 最终OTA共7文件：中文核心、nrchain、carrier、安装说明、合并许可、build-info、SHA256。独立Python ZIP reader验证CRC、全部内层文件集合及SHA256通过。不把测试日志/完整manifest/重复说明放进玩家包。
- 中文OTA：669621字节，SHA256 `e4a9bcd714f12198318ba8406a975bbc308caa2b8c06546acbfe624af3541d72`。机器版本`beta0.4.5-dx11-compat`，公开名`0.4.5-DX11-兼容增强`，实际核心PE0.4.5.104。carrier的1.4.12.0是其固定上游版本，不是安装器或核心版本。

本地完整日志在工作区同级`nr-main-unify-pass3.log`与`nr-main-unify-package-final.log`；完整source-manifest/validation保存在`nr-main-unify/build/dx11-compat-pass3`，ZIP与独立核验记录在`build/dx11-compat-chinese-final`。这些不进入源码Git。

保留失败批次：pass1在中文公开名称加入后暴露两个测试读取manifest时默认GBK，改显式UTF-8后通过；pass2 Windows暂存目录rename返回WinError5，未发布半成品，新目录pass3成功。精简打包曾捕获中文说明中PowerShell变量边界错误，已用实际PowerShell回归修复。不覆盖旧失败日志。

本轮未运行新的NVIDIA/游戏验收。旧严格post-SR snapshot门禁缺D3D12 Debug Layer的边界沿用历史说明；其他WARP通过不替代它。线上CI此前因账户付款/额度未启动，不计作通过。

## CI与PR来源

PR138原头ff6dade通过真实普通合并保留祖先；本轮只保留一个PR自动入口，24 YAML/20条workflow_call边检查通过，main原有门禁覆盖保留，发布写权限隔离。

关联任务已关闭七个重复入口#137/#131/#117/#114/#112/#111/#92；它们不是通过关闭PR就宣称原base合并或游戏问题已修。永久整理记录：https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/80#issuecomment-5568772224 。主线整合完成后#133/#138均以main真实祖先为依据收口。

本轮还保留main后来新增e32cc3f的35份BUG归档。私有仓库审计：622 tracked文件检查时，有4份历史反馈ZIP，全部原已在远端main/兼容线；本轮没有新增二进制/模型/运行库到Git。仓库可见性仍为private，不做public-source导出。
