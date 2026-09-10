# PR #244：按现用 048/HoYo 工作流复核后的反馈增量

## 基线与范围

本轮再次直接读取指定分支、最近提交和 PR。可读远端 `codex/manager-048-hoyo-unified-20260911` 仍为 `1cc34bf519d4056978e6806aadf8b5e958c71762`；反馈分支为 `2045431771f5bbb321a39424c86ac3419d4fa363`。未获得更晚的本地提交或可用 ROG 执行接口，不能把未读到的本地状态当成已同步。

本增量继续接在 #244，不把已存在的 Scanner、中央评估、操作预览、HoYoShade/提权工作流或 MFG 当成缺失功能重写。此次不改 main/preload/index、安装/恢复规则、NR/SR/FG 算法、默认组件与载荷；只窄改真实 HoYo 页面和本 PR 的反馈模块。

## 发现并实际修正的问题

### 1. 新版 HoYo 页面在失败时没有反馈入口

已读取完整 `hoyo-page-ui.js`，原始字节 Git blob 为 `6f4e00ae08a31ff59800606585b41b844d79ddac`。真实代码的 `ready()` 为真时才调用 `openSettings()`，因此旧 #244 把入口只附在 GamePageUi 上，会漏掉启动失败、恢复或等待状态。

现在由 HoYo 外层工作流持有一个共享反馈入口；嵌套画面编辑器显式委托，不重复创建入口。已绑定并有合法 `gameId` 的客户端在失败/等待/恢复/安装状态都可反馈。没有合法 `gameId` 的发现记录不冒充游戏库 ID，也不自动绑定或安装。

复用现有三项评分/预览/保存，不新增启动器、安装确认或主按钮。相同客户端的后台刷新及 ready→failed 转换保留评分与备注；换客户端、离开页面、收起详情及销毁时关闭旧反馈。现有忙碌/操作预览/编辑 API 边界继续禁用操作，不增加新轮询。

普通 GamePageUi 的摘要不再从分别缓存的 installation/diagnostics 字段直接宣传“本次 NR 成功”。用户体验和技术状态仍由后端在生成报告时分别记录。

### 2. 缺失身份导致数据库错误合并

旧 `exactGroup()` 在 EXE 哈希、商店 ID 均未知时没有游戏名称/EXE 后备身份，两个不同游戏可能归为同组；renderAdapter 未绑定时忽略显卡清单，使不同驱动归为同组；当前配置快照还可能与启动前快照混组。

新分组保留游戏/启动器/客户端后备身份、contextSource、未绑定 GPU 的型号/PCI/原始驱动清单。清单顺序和临时 inventory ID 不当作身份；未绑定仍为 `inventory-only` 和 incomplete，不猜实际渲染 GPU。已绑定配置仍按对应 GPU/driver 查询。未知不作为通配符或成功概率；没有自动默认路线写回。

同时拒绝非法 API/EXE hash、嵌套 OS/证据/警告对象、非规范化组件和非法计数，防止不合规报告进入统计。

### 3. 安装损坏时恰好无法收集反馈

组件清单或启动回执读取失败时，现在可生成明确的 manual-snapshot，并记录有限、固定的缺失原因；运行诊断失败时保留用户评分，不虚构 NR/显示/崩溃。错误游戏 ID 仍拒绝。

配置路径、运行目录、部署模式及游戏版本加入内部上下文指纹；预览后发生变化必须重新生成。原始路径只用于内部身份比较，不新增到公开元数据。部分读取失败不允许建立精确的启动快照。

## 本轮验证（与上次分开记录）

- Linux Node 22.16：102 项模型/包/分组/提醒/新增服务回归通过，0 失败。
- 原产品适配器另选 11 项回归通过。全套原 adapter 测试首轮有 3 项因本轮源码子集未包含 main/preload/index 而失败；日志保留。它们不是本轮运行过的完整 IPC/HTML 验收，不将随后排除的用例计为通过。
- 原有隔离 GamePageUi 合同浏览器回归：38 项通过。
- 新增真实生产 HoYoPageUi 浏览器回归：28 项通过，覆盖失败/等待/恢复/安装/就绪、共享入口、双客户端、保留备注、退出页面、实际 ZIP 保存及独立 Python CRC、900×620 与 390px 布局。嵌套 GamePageUi 与服务/硬件数据仍是替身，不冒充完整 Electron 或真实游戏。
- 负对照：相同新测试在原 HoYo 页面+旧适配器上因失败状态缺入口而失败；旧分组器把两个缺身份游戏合成 1 组而失败。原失败记录与修正后的结果均保留。
- JS 语法、git diff --check 通过。没有 Windows CIM、打包 EXE、真实 HoYo/FG/NR、游戏性能或账号安全结论。

运行命令：

```sh
node --test test/compatibility/model.test.cjs test/compatibility/package.test.cjs test/compatibility/reminder.test.cjs test/compatibility/048-followup.test.cjs
node --test --test-skip-pattern="new preload|real main registration|HTML loads" test/compatibility/manager.test.cjs
python scripts/test-compatibility-browser.py
python scripts/test-compatibility-hoyo.py
```

浏览器脚本需要测试环境的 Playwright/Chromium，非产品依赖；可设置 CHROMIUM_PATH 和 COMPAT_REVIEW_DIR。已有单测入口已纳入新增 21 项用例。

## 本地接续只做这些

1. 核对实际最新 Manager SHA，再在独立 worktree 集成本 PR。若本地有未推送的大更新，只移植这次 HoYo 的共享反馈生命周期调用与新模块，不把整份旧页面覆盖回去。若已含 20454317，只需后续提交；否则需要整个 PR。
2. 在完整 Windows 源码执行原 npm test、真实普通/HoYo 页面与打包后反馈测试。重点测 HoYo 失败时仍可反馈、合法 gameId 与发现 id 不混用、同客户端草稿保持、换客户端关闭旧预览、已存在文件/无权限保存。
3. 一款普通游戏与一个已授权 HoYo 测试环境跑反馈→ZIP→离线导入。人工检查 raw DriverVersion，缺实际 GPU 绑定必须保留未知，不人为改绿。

自动上传、退出后提醒、HoYo 提权跨进程快照、实际渲染 GPU 绑定仍是后续增强。原未完成项没有被这次窄修补冒充完成。保持 Draft，不合主线、不发布、不覆盖现用安装。
