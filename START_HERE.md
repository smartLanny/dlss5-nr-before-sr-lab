# 开发者从这里开始

仓库现已公开。当前源码身份为 **0.5.2 Beta17 开发候选**，以
`scripts/core-source-layout.json` 为准；已交付 Beta14 的源码、二进制哈希和验收范围
保存在[原交付回执](docs/interop/052_BETA14_DELIVERY.json)，不因源码更新而改名。

## 获取和检查

```sh
git clone https://github.com/smartLanny/dlss5-nr-before-sr-lab.git
cd dlss5-nr-before-sr-lab
git rev-parse HEAD
git rev-parse --is-shallow-repository
python3 scripts/run-portable-tests.py --verbose
```

公开源码无需维护者账号或私有交接包。若已使用浅克隆，先获取完整历史与 tags；
它们用于回归和分支归属检查。通过 `CXX` 选择主机编译器，Windows-only 与缺失输入
会明确跳过，不算通过。

## 阅读顺序

1. [AGENTS.md](AGENTS.md)：协作、资源生命周期和交付规则。
2. [当前状态](docs/interop/CURRENT_STATUS.md)：源码能力、基线与仍待验收的路线。
3. [源码结构](docs/maintenance/SOURCE_LAYOUT.md)：直接修改 `src/`，无需恢复旧补丁链。
4. [贡献指南](CONTRIBUTING.md)和[源码许可](LICENSE)：提交范围与第三方署名。
5. [网络选择交接](docs/validation/NR_CLOUD_HANDOFF_20261018.zh-CN.md)：DLL 身份检查与实机验证顺序。

## Windows 完整构建

使用 `scripts/build-unified-candidate.ps1`，按
[新机依赖说明](docs/handoff/BETA13_NEW_PC.zh-CN.md)准备各项固定输入。
该文档保留历史 Beta13 的来源与验证记录，不代表必须下载其私有附件；公开依赖各自
从原项目取得，维护者缓存与厂商运行库需另行准备。不要把缓存缺失改成跳过构建门禁。

[自托管构建说明](docs/maintenance/SELF_HOSTED_RUNNER.md)列出输入和完整入口。
交付应记录精确 commit、dirty 状态、源码清单、编译回执以及二进制/包的 SHA256。
CPU 测试和受控 WARP 不代表游戏、真实 NGX 网络或性能验收。

## 用 F8 文件整理反馈

游戏里的 F8 会保存一个 TXT。退出游戏后，也可以在任意装有 Python 的电脑上生成
便于提交的报告；原始文件保留，不需要重新压成 ZIP：

```sh
python scripts/feedback-report.py feedback.txt --output report.md
```

工具会检查原始 TXT 的结构和摘要哈希，区分完整与部分采集，再整理构建身份、请求
配置、旁路原因和调用增量。它不会上传文件或覆盖已有报告。用游戏保存的原始 TXT，
不要从聊天中复制后重存；改过换行的文本无法通过原始摘要检查。

画面异常可以加 `--symptom visual` 整理描述。如果补拍 F7，使用它新生成的一对
核心 ZIP 和图像 ZIP：`python scripts/feedback-report.py core.zip --image images.zip
--output image-report.md`。旧 F8 TXT 不能替代这对文件中的核心 ZIP。
详见 [F8 TXT 验证说明](docs/validation/F8_TEXT_REPORT_20261103.md)。

## 接续已有工作

先读取最新 main、开放 PR 和对应 Issue，再开短期任务分支。#341 的退休修复、#345/#347
的 SDR 色控与重置、#346 的网络选择有各自来源和验证范围，不要重复移植。
私有 SR、双 RR 与显存观察继续沿 #309 / #330 跟进；研究宿主通过不代表产品接入完成。

保留用户配置和历史包身份。主线合并、发包和 Manager 默认版本是独立动作；本轮源码
开发不自动改变用户游戏目录、运行库或 Manager pin。公开反馈先脱敏，原始采集留在
维护者受控环境。
