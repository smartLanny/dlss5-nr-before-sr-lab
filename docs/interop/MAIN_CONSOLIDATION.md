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

## 已执行交付与收口

main于8769456完成实际远端合并，之后的本页提交仅补充交付结果。33个已包含临时远端分支通过精确SHA租约的原子删除清理，逐项回查不存在；82→49。完整SHA及恢复依据见BRANCH_RETIREMENT_20260907.json，所有提交仍在main祖先中，本地旧worktree/未提交文件不动。默认fetch改为标准全heads映射，避免删除旧命名分支后后续fetch失败。

[0.4.5-DX11-兼容增强已发布](https://github.com/smartLanny/dlss5-nr-before-sr-lab/releases/tag/beta0.4.5-dx11-compat)，预发布、非latest。Tag实际指向二进制源码dccb5b3；唯一中文精简OTA上传状态、669621字节和服务端SHA256已核对一致。GitHub附件使用ASCII名称、中文显示标签，包内仍是中文核心。

main@8769456线上run34109054671已核实：三个job均0steps（其中构建依赖跳过），check annotation明确账户付款/额度阻止启动。没有把它写为代码测试失败或CI通过，也没有重复重跑。完整本地门禁仍是上文记录。

Manager配套在既有product/xiaofeng-manager-foundation分支直接快进至7e2d16e，没有新建长期远端分支或把整个Manager复制进Addon根目录。0.4.5-ota内部选择直接替换为公开同名新版，DX11不再静默落到旧0.4.2槽。旧0.2/0.3.3.5仍可选择；全局默认/已有版本锁保持原行为。安装保留INI、正确退役旧carrier，管理中的carrier不再误报冲突。载荷准备脚本先检查四个最终文件哈希，校验路径范围/重解析点，再执行隔离载荷替换。

Manager应用源码dfa0a2e，后续交付脚本/文档/辅助脚本提交至7e2d16e；本体版本0.4.5与内置Addon的公开名称分别记录。54项测试、UI契约、固定vendor和32项展开payload校验通过。父任务独立从实际portable解出新版payload与app.asar，四组件SHA及34个应用源码文件逐字节匹配。两种EXE内嵌FileVersion/ProductVersion均为0.4.5。

- 安装版：313681452字节，SHA256 `bc4bb56f6b464cf76066c9192d5e7248e9cfd00aba9649bcda58c046c10e2f26`。
- 免安装版：313344423字节，SHA256 `de69d61b3a3b7ff61bc71e0bed7fceece894ac60877cc021ec2e21c63c641942`。

本地安装器与交付收据在同级deliveries/DLSS5-Manager-0.4.5-DX11-compat-20260907。本机winCodeSign缓存解压缺符号链接权限，使用signAndEditExecutable=false完成未签名构建；NSIS/portable包装仍有0.4.5版本和管理员执行级别，应用保留自行提权。未做真实游戏安装，不以归档/主机测试替代实机。

PR#133/#138也已关闭并回帖指main，累计9个重复入口收口；剩余7个开放PR保留独立研究/游戏兼容/Manager评审。未关闭未经游戏验证的问题。

最终发布回查：同一Release已正式包含且仅包含中文精简OTA、Setup和portable三个附件；服务端uploaded状态、三个字节数和SHA256均与本地独立核验匹配。记录保存在build/published-release-verification.json。Main检出为原项目同级nr-main-unify并跟踪origin/main，原product及旧worktrees保持不动。

## 2026-09-08 安装器热修补充

上述三个附件记录对应首发状态。当前同一Release已追加 **Manager 0.4.5-hotfix.1** 的Setup和portable，旧Manager附件已标注改用hotfix.1；核心及中文OTA字节不变。Manager维护分支已快进到 `2ca90d1b32e16cfe332a068a5c01530b46595d64`，无新增长期远端分支。

本轮从两张玩家照片区分出 `ERR_GAME_RUNNING` 和全局“安装组件未准备”，不是反作弊确认错误。已复现并修复：移除旧DX11选择项后，前端误用残留设置，把后端完整组件改判未准备；游戏目录内便携启动器/安装器自身子进程被旧目录匹配规则拦截。新守卫只按PID关系与精确路径豁免自身，保留真正游戏及其他程序阻断；报错/反馈增加进程名和PID。若UAC使父进程关系不可确认，则继续保守拦截。不能仅凭截图确认该玩家的具体触发条件，也不宣称2K27已实机验收。

69项Windows测试及UI契约通过；真实CIM空目录查询通过。从两个最终EXE分别解包核对35个应用源码文件、固定vendor、全部组件SHA及12项真实归档renderer版本切换，均通过。界面/产品版本 `0.4.5-hotfix.1`，Windows文件版本 `0.4.5.1`，内置条目仍为 `0.4.5-DX11-兼容增强`。

- Setup：313729661字节，SHA256 `119e60f291c3f4ee111fda8cf4dfec587a552bfb92a827605db3ddacbed6f6fc`。
- portable：313392629字节，SHA256 `45d066176597cbaa2c51c1271698081eb031d51f7c8b1d885f479c9166a7a01b`。

两个热修附件已上传，同一Release服务端uploaded/大小/摘要逐项回查匹配。源码诊断记录在Manager分支 `docs/INSTALLER-HOTFIX-20260908.md`；本地交付与验证收据在同级 `deliveries/DLSS5-Manager-0.4.5-hotfix.1-20260908`，构建及发布回查在 `nr-manager-readiness-20260908/build/hotfix-*.log/json`。本次仍未在反馈者电脑或2K27实机回测。

## 2026-09-08 API 与部署热修 hotfix.2

推荐安装器已更新为 **0.4.5-hotfix.2**（Windows文件版本`0.4.5.2`）。内置插件仍为 **0.4.5-DX11-兼容增强**；核心、配套组件和精简OTA字节未变，没有混入其他任务的未发布核心/carrier测试包。Manager维护分支快进至 `8acf31f9cd6245a32256f28accbb2c0a49bc5386`。

修复`DirectX 11/12`和`dx12=true`误判纯DX11；API与核心版本分别决定，明确选择/版本锁不再被覆盖。DX12不部署carrier且保留nrchain，DX11要求包内成套carrier。静态EXE11仅是线索，综合引擎模块、启动配置、EXE绑定的历史ReShade日志；混合/未知由用户在游戏卡片指定API，并绑定EXE保存。安装、修复、OTA、版本切换走相同规则；保存API后需应用组件路线才允许Manager启动已安装游戏。

旧carrier可逆移出，受管文件改动/备份损坏先停止；冲突移动纳入真实journal，卸载不提前销毁持久原始备份。明确DX11部署只解除配套carrier精确`@文件名`禁用并保存ReShade.ini侧备份，保留重复section、其他项及逗号转义。ReShade运行时预加载防护由PR#160独立维护，不把它描述为已经实现全自动API激活。

119项主机测试与UI契约通过；两个最终EXE分别解包核对37个应用源码文件、完整载荷SHA及12项归档版本选择检查；从成品提取的服务代码再次通过2条完整API/OTA事务链。实际Electron界面夹具验证API保存、DX12启用安装及独立核心版本显示，并保存截图。所有这些均不代表燕云、2K27或其他游戏实机兼容性通过。

- Setup：313737210字节，SHA256 `1435758165c4c0c62025fa9bceac0d8ebbe820bf7f5d6bbb7c6e242c76d4a4cc`。
- portable：313400179字节，SHA256 `63972e26c21ca943fcb9632ef2ab22e0327716c166bc0ea6d2af55586256b7da`。

同一Release已上传两个hotfix.2附件并回查uploaded/大小/服务端摘要匹配；旧安装器标为历史，发布页顶部推荐hotfix.2。源码说明在Manager分支`docs/INSTALLER-API-ROUTING-20260908.md`；本地交付`deliveries/DLSS5-Manager-0.4.5-hotfix.2-20260908`，构建/成品/UI/发布收据在`nr-manager-api-routing-20260908/build/api-*`。主线同步保留了其他任务后来新增的#158归档。
