# beta0.5-dev3 稳定化候选

## 来源与边界

基于 #115 `962f15577ac86b2f9ca6d1d2d670714ee0ca0cb6`，前向合并 #117 的0.4.5候选 `12009d0448fb8bba8c699020382bf5bb6b84dec4`，包含 main `381223472e941d85cb08faa493d469661f0782b7` 新增原始证据。保留双父 ancestry，不强推main，不合并上游PR，不发布OTA。准备工具不进入候选源码。

## 原始证据改变的判断

| Issue | 归档事实 | 处理 |
|---|---|---|
| #38/#64/#101 | test1前置曾提交NR，后置采集窗口计数停止；native Reset、guard齐全，dynamic=0x8 | 继承0.4.5有界viewport/scissor/blend恢复，未知/污染状态仍旁路；不宣称目标游戏已修好 |
| #97 | HTGame 4080SUPER的运行库加载失败，历史bypass文字滞留；计数0 | 分模块即时Win32、缺失export、Init阶段诊断及配套自有bridge；GetLastError为0则标一般失败，不臆断缺DLL |
| #98/#99 | 采集窗口NR与提交分别+36/+32，首尾active | 不误判永久Hook失败；继续记录间歇性状态，不移除queue-affinity保护 |
| #100 | 一个Issue含StreetFighter6旧0.4.3、HTGame r2/r3三个不同包；r3窗口+45 | 按exe/build/session逐包归因，不能将一个文件夹里的所有问题算同一个游戏 |
| #39 | 实际NR/提交窗口+35，画面黑边/颜色问题与Hook完全不启动不同 | 保持图像问题开放，不用计数增长冒充画质正确 |

源文件在各 `bug-inbox/issue-N/20260906-unpacked_*__capture-info.txt` 与 `__logs__nr-current-summary.txt`；部分归档为有界摘要/分片，而非全GPU输入或转储，不把“文件增加”称为已经拿到完整视觉复现。归档byte-for-byte保留。

## 本候选代码

- 继承#92嵌套原生透传、粘性列表补登记、受限空PSO路径，#111失败释放保留依赖/隔离，#112上限4，#114推荐按钮，以及#117混合状态、运行库、F8 TXT与显式颜色试用UI。通用注入风险仍需实机验收。
- 修正0.5接合遗漏：推荐设置显式恢复高强度保护、关闭分层并将四项归1，不清理故障隔离，也不改变总开关、深度/jitter/R8准入或反馈快捷键。
- F8核心TXT/ZIP增加相同Present锁内快照的分层/保护/未验证颜色请求。拿不到快照则不伪造这些设置。F7同时记录请求保护值和该帧codec标志，不从当前全局配置冒充旧帧。
- 报错修正：调用失败且Win32错误为0时用ERROR_GEN_FAILURE，不虚构ERROR_MOD_NOT_FOUND；真实非零码与成功结果不变。
- 生产分层shader与dev2字节相同，35常量ABI、资源/dispatch数不变。没有新增神经推理或颜色公式，不宣称金属脸、所有HDR黑边或RTX40模型支持已解决。

## 验证

本地已执行生产推荐函数408项、picture/loader33项及独立JSON解码，wrong-layer与wrong-default负对照；既有透传38、状态75、释放70、cap27、分层155748、boost1044、lighting35012等回归。脚本数字不是游戏数量。完整Windows/SM5/WARP/静态分析/打包结果以对应PR的固定HEAD CI为准，不由本文静态声明通过。

新增Windows核心诊断测试实际检查磁盘TXT等于缓存、ZIP的冻结分层值、采集后全局配置变化不会污染报告。继承的完整WARP正负对照继续执行，包含0.4.5新增blend-only像素和错误回放负对照、颜色显式请求、TXT部分/独立失败测试。

## 手动验收与回退

见 [2077本机测试](testing/CP2077_DEV3.zh-CN.md)；B/C/D固定变量先于FG/HDR/后置压力测试。测试包只供实验，不替换已发布版本。关闭分层可回退算法；资源隔离/硬异常必须完整退出游戏，不能用F6清除。真实NVIDIA模型、2077与其他目标游戏、FG及帧时间仍未验证。
