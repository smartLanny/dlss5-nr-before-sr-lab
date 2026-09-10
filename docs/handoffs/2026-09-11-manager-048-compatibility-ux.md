# 048 UI 与兼容反馈增量：本地验收交接

## 接续身份

- 仓库：`smartLanny/dlss5-nr-before-sr-lab`，现用 xiaofeng Manager。
- 精确父提交：`1cc34bf519d4056978e6806aadf8b5e958c71762`。
- 父分支：`codex/manager-048-hoyo-unified-20260911`。
- 工作分支：`codex/manager-048-compatibility-ux-20260911`。
- 不修改 Core、默认 NR 处理者、载荷、GPU/驱动配置、安装/恢复规则或用户当前安装。未合并主线、未发布。

## 已接入，而不是一份待运行的安装脚本

`main.js` 在现有 IPC 白名单/来源检查入口增加 open/preview/save/discard/close；既有 `beforeLaunch` 增加只读配套快照，失败不阻止游戏启动。`preload.js` 只暴露五个对应方法。`index.html` 在原 GamePageUi 后、HoYoPageUi 与 renderer 前载入组合适配层。

新增 `src/product/compatibility-feedback.js` 直接使用现有 GameAssessment、launch sessions、expected module manifest、runtime verifier 和原反馈采集器，不另建安装器或运行检测器。它保存只读反馈上下文，不写安装 journal。

界面组合适配保留原 host、原控制器/DOM 深度、请求字段及委托事件。普通游戏、HoYo 游戏设置和维护页复用这一入口。不会把未验证的整套外部 NR 自动换成默认后端。

## 用户能看到的变化

1. 图形 API / Core 配套改用“图形接口 / 增强版本”，当前选择及默认自动检测不变。
2. 日常首屏保留画面风格、AI 效果强度及原有人脸调节。明暗、细节、处理分辨率和前后置效果项进入“细节、性能与更多设置”。不是删除参数、重解释 INI 或重设默认值。
3. 沿用现有唯一安装/应用/启动主动作，不增加另一个一键安装器或外部后端选择器。
4. 游戏设置底部可“反馈本次体验”：正常游玩、画面表现、流畅度三项，不确定可跳过；异常时才展开标签；备注可选。相关日志默认不勾选。
5. 预览自动元数据、评分及可选日志后，确认保存 ZIP；使用原生系统保存框，不自动上传，提示发给测试群管理员。刷新页面不会清空正在填写的反馈，换游戏关闭旧反馈。

## 数据真实性

- 每份报告带游戏/客户端信息、API、Manager/build、已声明组件版本/hash、配置指纹、OS、显卡与驱动清单、会话、主观评分、技术证据和隐私声明。
- Windows 驱动使用固定只读 CIM 查询，5 秒与 64KiB 上限；查询失败仍可反馈。保留 raw DriverVersion，不按未经核对的规则冒充 NVIDIA 展示版本。
- 不把第一张显卡当作实际渲染 GPU。当前接入未取得运行时 GPU 对应关系，`renderAdapter=null` 是正常且诚实的结果。
- 能匹配真实启动前的配套/配置时绑定 native session UUID；缺快照、重启管理器、配套变化或历史会话时，明确是“当前配置快照”。此类包不会作为确切运行组合自动认证。
- 已有 runtime verifier 的 loaded/NR 信号可以被记录，NR 仍只证明该会话中计数被观察，不证明最终显示、实际帧参数或画质。SR、RR、FG、最终呈现没有匹配证据时保持未知，不用显卡代际或安装状态猜。
- 原反馈采集器可能带历史日志，因此日志附件始终标为 historical，不作为当前会话成功证据。日志基础遮蔽不保证完全脱敏，公开前仍需人工检查。
- 预览后配套、目标 EXE、SR/FG 请求或 native session 改变会拒绝保存，要求重新打开；不会串到另一游戏或新版本。
- 组件 manifest 是声明而不是本次真实加载证明；未知游戏版本/EXE hash 不伪造。

## 团队收包与离线汇总

```sh
node scripts/import-compatibility.cjs --input <收到ZIP的目录> --out <私有输出目录>
```

导入器校验本工具生成的 STORE ZIP、CRC、清单/hash、大小与路径边界，不提取任意压缩内容；只处理本契约，不宣称接收所有旧 ZIP。保存不可变报告 JSON，按报告 UUID/会话去重。保留原始 ZIP 供日志复查；工具不删除输入，不自动把附件上传公开仓库。

汇总按游戏版本/EXE、API、完整组件方案指纹、Manager、系统和实际 GPU/驱动组合分组；缺关键条件保持 incomplete。主观评分、技术状态、冲突、未知分别统计。匿名会话不换算独立用户，同一会话的矛盾反馈保留待审。

`decision.cjs` 与 `reminder.cjs` 是纯数据/策略测试辅助：没有替换现有中央决策，也没有接入退出检测或定时弹窗。本版本没有自动上传 API、网站、GitHub token、社区默认路线自动写回或截图/dump 自动采集。

## 已执行验证与边界

- Linux Node 22.16：`node --test test/compatibility-ux.test.js`，95 passed，0 failed，0 skipped。
- 实际改后的 preload 通过 VM 调用；实际改后的 main 的 registerIpc 通过有效/伪造 sender 用例。其他项目服务在这些测试中为隔离替身。
- 新增实际 UI 模块 + 新增 048 适配器 + Node stdio 服务：离线 Chromium 38 项检查通过，包括原控制器/事件保留、主按钮唯一、更多设置开合、无 MutationObserver 空闲循环、三项评分、日志 opt-in、刷新/换游戏、真实 ZIP 保存与独立 Python CRC 检查、900×620 与 390px 窄窗。
- UI 截图使用原控制器 DOM 契约夹具，不是完整现用 Electron 或真实游戏画面。没有声称执行了全仓原有测试、Windows driver/CIM、真实 Electron 打包、ROG 或 GPU 渲染。
- 未下载/使用 NVIDIA 二进制。原 main/preload/index 的读取字节均与父 Git blob 一致，改动集中于接线。

## 本地 Agent 只需接续这三组

### 1. Windows 全项目与真实 UI 回归
在独立 worktree 合入本增量，不覆盖活动工作树。执行原有 `npm test`、`npm run test:game-page` 及当前打包验收；重点检查普通/HoYo 设置均有单一反馈入口，原主动作/草稿/焦点/更多参数保持，换 API/Core、恢复失败、后台刷新不串数据。新增报告组件已被既有 `src/**/*` 打包规则覆盖；不要新增第二套构建或恢复器。

### 2. 两款已有可用游戏跑“启动→反馈→导入”
一个普通 D3D12，一个现有 HoYo/外置/Legacy 路线。先核对预启动快照与 actual native session、组件身份、raw driver。游戏外改 INI/换版本再反馈必须明确降为当前配置快照。分别试不附日志/附日志/取消保存/已存在目标文件/无权限目录。保存失败不能留下“已上传”提示。

HoYo 一次性提权 worker 当前没有新增跨进程快照桥，缺证时会降为 manual-snapshot；不要为凑绿灯把它标成匹配成功。后续优先复用既有跨进程回执补这个窄边界。

### 3. 后续增强，不阻塞本版
从现有 runtime receipt 接实际渲染 GPU/driver、真正的游戏版本/EXE hash、完整正在使用的组件清单与结束时间；自动退出后轻提示、同意后稳定匿名测试者 ID、可信上传接收端另做小切片。运行验证或上传缺失不影响用户手动三项反馈与 QQ 收包。

不能把本轮 95/38 项结果外推为 Windows/游戏兼容性，不能把诊断 unknown 变成 false 来制造成功率。
