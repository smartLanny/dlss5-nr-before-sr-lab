# DLSS5 NR-Before-SR Lab — Core 0.5.x

本仓库维护 DLSS5 NR → SR / SR → NR、多层增强、兼容与诊断的 Core。
`main` 是唯一持续集成分支；0.4 已冻结。安装管理器独立维护在
`smartLanny/DLSS5-Manager-ZJZ`，不在此仓库继续分叉开发。

**main 当前为 0.5.2 Beta 14**，维护者于 2026-10-03 接受其作为后续开发基线。
已交付二进制源码为 `3663affd`，与 main 基线 `024f9cc3` 的 Git 树相同。
功能说明简短、详情悬浮，状态与计数直接显示。双语 Core OTA 按版本保存，
RTX 40/50 与 RTX 20/30 的固定 NR 运行库独立保存、不随 OTA 重复；新机开发从
[START_HERE.md](START_HERE.md) 开始。

Beta14 完整双语构建及 5090D 受控调用已通过，新增 F8 菜单过渡记录和部分分配账目。
维护者接受异环本轮 Esc 表现；已知分配在切回单层后回收。默认路线保留 Beta13 的既有验收，
不扩大为所有显卡、场景或实验功能通过：私有 SR 碎边/闪烁、显存优化、长期增长和整帧
成本仍未解决。普通恢复仍为建议基线。公开包暂不附带 NVIDIA 运行库；Manager
默认版本在 Manager 仓库调整。历史 UI1/Inbox1/Beta1 保留原身份。

## 从哪里开始

- [新电脑接续、依赖和已知问题](START_HERE.md)
- [当前状态、能力与待办](docs/interop/CURRENT_STATUS.md)
- [UI1 操作、原包身份与测试范围](docs/interop/051_RECONSTRUCTION1_20260925.md)
- [主线归位、归档和磁盘整理](docs/maintenance/EXECUTION_20260926.md)
- [开发协作规则](AGENTS.md)；任务总入口 #294

## 构建和测试

使用 `scripts/build-unified-candidate.ps1`，提供固定的 ReShade、工具链、
DirectXMath、人脸后端、FFX SDK/缓存等输入。原始源码、生成源码、最终
二进制和包身份都需核对。不要关闭模块或跳过门禁来掩盖构建失败。

Windows 验证为主。GitHub 自动入口只覆盖 main，并通过仓库变量
`ENABLE_HOSTED_CI=true` 显式开启；当前维护者未付费启用托管任务。
禁用/跳过不是通过，不自动改写已有 CI 结论。

## 发布和隐私

公开 Core OTA 仅以核验后的包作为 Manager 仓库的测试版发行附件；不上传
本仓库历史、玩家原始反馈、厂商模型/运行库或本地路径。测试版不自动替换
稳定版本、组件 pin 或任何游戏文件。包内授权材料与已有签名机制保持，
SHA256 校验不等于数字签名。

当前版本为 `0.5.2-beta.14`（界面“0.5.2 Beta 14”）；后续行为改动从 beta.15 递增，不重命名
已有 0.5.1/Inbox1/UI1 二进制。持续旁路、闪烁或异常请保留 F8、日志和版本
身份后回滚；旁路产生的高帧率不是优化成功。

历史 README 原文见
[归档](docs/history/pre-mainline-20260926/README.pre-mainline.md)。历史文档的
版本名与链接属于原发布时点，不代表当前代码、验证状态或下载推荐。
