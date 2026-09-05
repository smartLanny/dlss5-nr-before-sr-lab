# 2026-09-05 方向优先级与分支执行记录

本记录基于本轮已读取的 Issues #2、#4、#7-#19，以及 `codex/beta0.4.1` 的实际远端提交 `175173f457f532a3d46800d0906047d3ae7f375d`（beta0.4.1-r2）。旧 RFC 中的 e0b94ff 是历史基线，不是本轮开发起点。排序是本轮建议，不擅自关闭或覆盖其他人的议题。

## 判断原则

优先保护现有 NR-before-SR 优势，再按覆盖面、可复用性、运行时依赖和合并风险排新功能。分开两种优先级：发布前必须验证的 P0，以及当前可并行实现的新功能 P1。不能因为能写 CPU 测试就把真实游戏回归降级，也不能等全部研究结束才开发独立控制模块。

| 顺序 | 议题 | 执行判断 |
|---|---|---|
| 发布 P0 | #7、#17-#19 | 冻结 r2 及后续修复产物，复核崩溃、NR反复切换、module-observer-uncertain；按实际日志/产物追踪，不认定已修复 |
| 发布 P0 / 可并行 | #9、#10 | 建立 GPU阶段毫秒与同帧 guide/曝光证据；它们是性能/画质宣称的门槛，不用回调FPS代替 |
| 扩展 P1-A | #14、#4 | 第一个非DLSS原生时序输入：D3D12 FSR3.1 → DLSS SR，先NR关闭验证基础替换，再接NR |
| 产品 P1-B | #13、#16 | 统一请求/实际状态、版本化控制、F8锁、真实R与N；复用PR #5/#6，不另起完整管理器 |
| 功能 P1-C | #16 | 先原生FG/40系外部MFG共存，再通过已验证的外部ABI控制；不先合并所有hook/patch源码 |
| 兼容 P1后续 | #4 | UE DX11 TAA原生guide提取 + D3D11/D3D12传输 + SR载体，之后引擎profile；这才是老游戏的重要扩展，不等同于FSR适配 |
| 小范围兼容并行 | #2 | 审计已有helldivers2分支，择取通用typed-view修复，不重复写vtable伪装、不在未知合同下放宽准入 |
| 画质 P1后续 | #8、#10 | 固定N/数学，做Area/Lanczos3-AA与双线性/Mitchell的2×2对照；有时序证据和GPU计时后决定默认 |
| 优化 P2 | #11 | 有界编辑场预计算单独A/B，不能当作等价重排，也不与换核一起改 |
| 独立研究 P2 | #12、#15 | 低分辨率后置NR、RR兼容各自observe→identity→增强，不挤占首个非DLSS输入和统一控制落地 |

## 本轮开发的三个切片

1. `codex/research-temporal-provider-v1`：新增统一时序元数据准入、受限FSR3.1语义规范化、R→N motion/jitter计算及测试。**未接真实FSR拦截、DLSS SR载体或游戏运行。**
2. `codex/research-fg-mfg-control-v1`：新增能力约束、FollowGame/Off/Fixed/Dynamic区分、变更去重、请求/接受/观测三层状态；外部ABI未知时任何调用均拒绝。**未接Streamline或RTX40MFGCore。**
3. `codex/research-unified-controls-v1`：在FG分支上叠加统一菜单数据模型、独立R/N规划及真实资源观察后提交的事务。**未接游戏内菜单、NRUI扩展或游戏分辨率控制器。**

依赖关系：时序分支独立；FG分支独立；统一控制依赖FG。前三者只增加 research 头文件、CPU测试和文档，不修改发布版shader、hook、Mode1/Mode2、RR旁路、作者身份或安装渠道。当前不是新功能发布包，也不代表适合不经审查直接合并。

## 为什么不先宣布“所有老游戏都能DLSS+40系MFG”

有FSR/XeSS原生guide的游戏、只有TAA的DX11游戏、仅能拿到后处理画面的游戏，是三个不同难度的合同。第一类可先证明真实SR替换；第二类还需原生深度/速度/jitter提取和跨API同步；第三类合成输入只能作为标明质量级别的后备。降低N不等于游戏真实R变小。

上游 `dashdogy/RTX40MFG-Unlock` 当前V1.2在已存在Streamline FG的游戏上提供实验性40系控制，不替没有FG的游戏补完整FG集成。已核对提交 `4e776d068f91b4a665425542bb005dd57cc3d891` 的独立核心、V3查询ABI与状态结构。优先做可选外部客户端并验证实际显示节奏；不复制其整个patcher、不把混合插帧标成原生MFG、不把错误倍率上限暴露给玩家。

## 下一批运行时PR的边界与验收

- FSR observer：只读、版本绑定、单游戏、同帧/适配器/队列/格式/曝光/深度/MV语义；失败保持原FSR。
- DLSS SR carrier：先NR关，验证R→O、DRS/cut/HDR、参数恢复与生命周期，再接当前NR。无原生DLSS调用不再是唯一输入前提。
- External MFG client：精确ABI/size/代码身份、早期加载、可调用函数与模块生命周期、独占控制、实际runtime上限、Reflex/UI数据、FollowGame恢复。NR关闭不擅自关原生FG。
- NRUI扩展与菜单：以r2重新审查现有PR #6；加版本化扩展而非破坏V1；请求排队到明确同步点，R改变前暂停受控FG，成功观察/重置/预热后恢复。
- UE DX11样本：先一个TAA提取与同适配器共享纹理/fence合同，确认真实场景R可控后再扩到第二游戏；没有可靠guide就不冒充DLAA质量。

每条运行时路径须单独记录自动测试、Windows构建、WARP和真实游戏结果；基准关/NR/FG/NR+FG分组，验证开关、切分辨率、切窗口、菜单/读档/退出及长期资源增长。真实显示间隔与基础渲染率分别测；截图、调用成功或FPS上升都不是完整验收。

## 已执行验证与限制

本轮便携C++17三套独立CTest均通过；GCC Release与Clang AddressSanitizer/UndefinedBehaviorSanitizer通过。最终CHECK执行次数：时序5151、FG1001、统一控制1038，共7190次；多数为参数遍历，不是7190个独立游戏场景。

DevSpace两次连接均返回网络错误，因此本轮没有Windows add-on编译、NGX/WARP运行、40系显示节奏或任何新增兼容游戏实测。GitHub写入已使用独立分支；不借用旧构建结果，不发二进制，不提交上游实现/运行库/模型/测试者原始文件。当前缺口在各分支文档里保留为明确的后续验收项。

## 一手参考

- FSR输入： https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/
- NVIDIA FG与时序： https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md
- RTX40上游： https://github.com/dashdogy/RTX40MFG-Unlock
- 上游V3 ABI： https://github.com/dashdogy/RTX40MFG-Unlock/blob/4e776d068f91b4a665425542bb005dd57cc3d891/source/native/backend_bridge.h

这些是设计/接口参照，不是本仓库已完成对应运行时能力的证据。
