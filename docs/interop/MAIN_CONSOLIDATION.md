# 2026-09-07 main统一与dev4维护

维护者明确授权将最新版本收敛到main，改善F8反馈与稳定性。当前工作基线7b9ebb8普通合并c03e3ab（dev3/fix2全部祖先）与原main 4e11c35；main独有50提交、52个bug-inbox文件完整保留。

## 分支处理

main是唯一持续开发入口。历史源SHA及处理计划见BRANCH_SNAPSHOT_20260907.json；仅当分支当前SHA仍匹配快照、且完整属于验证后的main祖先时才删除已整合临时远端分支。现有本地worktree、分支及未提交文件保留。旧release基础分支暂保留以免影响未完成PR，但不再独立积累维护修复。

0.5分层算法、Manager、Feeder和其他未合入实验保留；本轮不会把未完成路线按版本号混入生产兼容核心。PR #133/#137/#131及其他已包含维护PR由main实际祖先关系收口。

## 版本与证据边界

本轮候选beta0.4.5-multiapi-dev4，核心PE 0.4.5.104，carrier使用独立dev4文件名。dev3 Release/Tag b03f474保持原字节。dev3游戏测试仅作为基线，dev4不继承实机通过结论。构建/打包使用干净固定提交，自动证据为source-manifest.json、validation.json和SHA256.json。

本轮反馈变化和最终实际验证结果会在合并前补全。GitHub CI和本地门禁分开报告；不因源码合并关闭仍未验收的游戏Issue。
