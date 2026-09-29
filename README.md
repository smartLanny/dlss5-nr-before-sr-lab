# DLSS5 NR-Before-SR Lab — Core 0.5.x

本仓库维护 DLSS5 NR → SR / SR → NR、多层增强、兼容与诊断的 Core。
`main` 是唯一持续集成分支；0.4 已冻结。安装管理器独立维护在
`smartLanny/DLSS5-Manager-ZJZ`，不在此仓库继续分叉开发。

**本开发分支最新候选为 0.5.2 Beta 13。** 产品源码 `cd8ba700`，
功能说明简短、详情悬浮，状态与计数直接显示。提供 RTX 40/50 与 RTX 20/30
两份手动发布包，同版双语 Core、各自固定 NR 运行库；新机开发从
[START_HERE.md](START_HERE.md) 开始。

完整双语构建及两套运行库的 5090D 受控调用已通过；这不是所有显卡或游戏
验收。Esc 过渡丢 NR、长期内存增长和实验算法的质量/成本仍有未解决事项。
公开 OTA、main 集成和 Manager 默认版本不随本交接自动改变，历史 UI1/
Inbox1/Beta1 保留原身份。

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

本分支已递增到 `0.5.2-beta.13`（界面“0.5.2 Beta 13”）；后续按新候选递增，不重命名
已有 0.5.1/Inbox1/UI1 二进制。持续旁路、闪烁或异常请保留 F8、日志和版本
身份后回滚；旁路产生的高帧率不是优化成功。

历史 README 原文见
[归档](docs/history/pre-mainline-20260926/README.pre-mainline.md)。历史文档的
版本名与链接属于原发布时点，不代表当前代码、验证状态或下载推荐。
