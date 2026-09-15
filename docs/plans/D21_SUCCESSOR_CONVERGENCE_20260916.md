# 0.5 大更新收敛计划：兼容输入、稳定多层与私有 SR

本轮范围：基于当前 GitHub 的 D21 继承线，审视维护者提供的 RenoDX 对齐方案、#258/#259 当前 HEAD 和 wilsjo2 v0.8.4，形成可执行的集成范围。本文是实施计划与复核结论，不是运行版本、全量代码审查通过或游戏验收报告。

本文与 `docs/research/D21_AMDOF_QUALITY_20260915.md` 配套；涉及本版优先级、私有 SR 定位与附件的推断边界，以本文为新索引，旧文保留历史。收敛到六个工作包，不再为相同机制新建一轮空壳框架。

## 1. 冻结证据与当前状态

| 对象 | 本轮读取身份 | 结论边界 |
|---|---|---|
| main | `dae86e9898399c20e52ada54a5ec4350da4d4fa1` | 当前 main，不等于 D21 运行源码 |
| D21 | `cbfb33919696a2840ed5154c91abaea56228b661` | `beta0.5-dline21-223fix2`，运行基线不改 |
| #276 | `c7b5acde42fd23f4774122f96e3571226ab85223`（本轮增量前） | 研究文档与 CPU 诊断，无新运行算法 |
| #257 | `92179ddd03617a78acdeac47ad1fbbe46cb11434` | D16 Present 有界重探；移植前核对 D21 路由差异 |
| #258 | `c7d2537f5d754426d7efd4b9a9a12b07f0fc069c` | 17 文件；已有 RR shader、帧合同、真实 D3D12 录制后端；未接游戏/Core |
| #259 | `1d1258282419d291004d38d545a8d5fc935a1c31` | 8 文件；有界仿射载体与尾差、合同/测试；无私有 Feature1 后端 |
| #278 | `1790830dd15c2b57831acef479a7a68e7fc2a6ed` | 新出现的菜单省电候选；独立回归，不默认混入 |
| wilsjo2 | `8802b2b470db0462fa1ed03a125e793a7c06d735` / v0.8.4 prerelease | 按发行提交看，不按较旧默认分支 |
| RenoDX 公共库 | `e5d66cb` | 已读取 resource/resource_upgrade/streamline_v2；不等同 SF9.8.5 完整 NR 源码 |
| Magpie | `3841698348bfb246623d4acf791984c8b68a577b` / v0.6.8-experimental.1 | 算法对照基线；不继承它的游戏支持声明 |

附件 `RENODX-ALIGNMENT-PLAN.md` SHA256：`3484ac4d8e4fd7fab062c664b37c506d2281bc9bdb62f5c9240b6c9c2e98ccf1`。只记录身份和复核结果，不把含私人路径、二进制定位或原始反馈的附件原文推入源码树。

## 2. 本版必须交付的产品结果

1. 兼容模式：逐项优先可信原生 Depth/MV，其次 ReShade 与 AMDOF，NVOF 可选。缺失任何或全部导引时仍有独立 NR-only 降级；不能仅显示原图就宣布无导引支持完成。
2. MultiNR：保留完整图串联、独立 Feature/历史；AMDOF 光流累积/累积+是本版核心，低频时域重建是互斥候选，不靠全局降低 NR 强度掩盖闪烁。
3. 画质：修清实际颜色消费者和保护预算，提供可归因的人脸/纹理对照；增加直觉化总饱和度与高级分层调节。
4. 私有 SR：升为本版可运行实验交付。先接“原生 SR/RR 后低分 NR 的修改层放大”，再复用同一后端接 Deferred B；#258 跨 RR 为独立高级路线。
5. 性能：保持相同实际 NR 工作量，治理代次留存、未消费中间纹理和不必要复制/等待；提供双层均衡、三层增强，低分预设可温和锐化。
6. 部署：一个 Core、按能力选择的 API 适配/配方；Manager 部署更新与回滚，不同时加载多个 NR owner。已有 FG 优先，额外 FG/MFG 不阻塞本版核心交付。

“大更新”是一份共同验收的版本，而不是一个难以回滚的巨型 PR。既有前置/后置/Present 和保存配置都保留，不自动改稳定版、main 或游戏安装。

## 3. RenoDX 兼容方案：保留方向，修改执行判据

附件提出“族表 + 降级、换资源不换通路、可选数据可旁路、分级失败”。采纳这个产品方向，但不能把以下推断直接变成生产补丁。

| 附件主张 | 本轮核对 / 实施决定 |
|---|---|
| sRGB/BGRA/UNORM 是同族，一律接受并重映射 FP16 | RenoDX 表是资源升级映射，不是任意原资源可创建的视图表。RGBA8 与 BGRA8 分属不同 DXGI 族；RGBA8→RGBA16F 需真实转换。D21 已有族表，优先接完调用与写回，不重复造表。 |
| 视图失败只告警即可继续 | RenoDX OnCreateResourceView 会选择原资源格式或升级目标；不证明我们失败的 GPU 操作可继续。补合法 canonical resource 与写回闭环，不只改日志。 |
| 强制 autoExposure 可解决 NR 的所有曝光门禁 | 引用函数是 slDLSSSetOptions，修改游戏 SR 设置；不是 Feature18 任意输入的证明。原生 SR/RR 参数默认不改；NR 代理亮度准备与 SR 自动曝光分开。 |
| 交换链色彩空间足以解释所有输入 | 只对确定来自该输出边界的图成立。引擎 pre-tonemap Color、RR 输出和私有 carrier 各有独立编码/曝光域；Present 可提供显式兼容配置，不把猜测标为原生。 |
| 扫出 61 个字符串即可删除 17 键、断言 preset 有画质效果 | 词表是候选清单，不证明字段被读取、类型、范围、有效阶段或当前版本效果。Width/Height 本身就在清单。保留 ABI、自有 jitter 相位处理；受控差分后才删无用写入。 |
| ControlMask 存在就是通用人脸强度 mask | 先核对通道、极性、格式、范围、坐标与生命周期；不能把光流置信度或肤色掩码直接塞进去。 |
| 队列错配连续 N 次才隔离 | 短暂缺证据可重试；已录制后出现真实设备/队列矛盾必须立即保护该工作，次数阈值不能使其安全。能证明独立的其他路线继续，未知不能越过 GPU 完成条件。 |
| 驱动支持 Feature18 就不要再运行我们的 NR | 能力可用不等于已有消费者正在执行。以实际 source ownership、Feature 与结果关系判重，不能按能力枚举关闭自有功能。 |
| R8 能 typed-load 就是有效曝光值 | 存储能力与曝光语义分开；可读且语义成立时原生读。不可证时转已验证显示参考/显式兼容路线，而不是强读或全局强改曝光。 |
| Hue 函数一加就能解决发灰；改默认 auto_correction=1 | 可作为独立合成对照，不是所有肤质差距的因果证明。正的均匀 RGB 缩放并不必然改变色度。默认值必须经 raw-vs-final 与近黑/高光验证。 |

通用成功口径：当前源帧得到正确 NR/合成并实际写回，原游戏链继续、可关闭恢复、运动稳定。把 rejected 改为 assumed/bypassed 或只看到 Feature18 计数都不算修复。

### 3.1 可执行的格式/颜色接入顺序

`合法原生 view（优先零复制） → 同格式合法快照 → 显式转换到自有 canonical 工作纹理 → 模型/合成 → 正确编码 → 原格式写回或受支持结果替换`。

- 无原生 UAV 不应阻断整条 NR：检查自有 UAV + 明确输出格式编码 + 合法 copyback/RTV。
- 非零 subrect、array/mip、MSAA 分别处理活动区域、所选子资源和 resolve；不能仅修改 desc 或拿 CopyResource 当转换器。
- UI、Depth、MV 与 Color 的转换含义不同；不平均深度边缘、不重复缩放 MV、不丢有效区和相位。
- 不修改游戏所有纹理的格式；先在私有副本解决，需全局升级的方案另审。

### 3.2 兼容性不只在格式

复用报告提供的源选择、私有输出、owned temporal lease、同 adapter DX11/DX9 传输、早期加载、模块多入口观察和有界资源池经验。具体交付包括：

- 在原生 guide 仍有效的 tag/evaluate 边界复制，绑定 source/view/device/epoch；不等 Present 再引用已复用的游戏纹理。
- D3D12 直接执行；DX11 先利用已有私有 D3D12 传输；Vulkan 与 DX9/32 位优先复用已锁定 Feeder/Bridge/wrapper 组合。
- 无导引分支创建初始化过的占位输入，显式标记 Synthetic/Dummy；缺失深度不启用依赖真实深度的拒绝，不关闭基础 NR。
- 观察/执行的 wrapper/native 身份映射、AddRef 归属与 command tracking 同时核对；统一解包不能绕开 ReShade 的必要观察。
- NR runtime 缺失、瞬时导引丢失、预算满、opaque GPU 错误分别处理；恢复触发尽量用事件/代次变化，避免每帧重建失败。
- 无可靠私有 NR 成果时保留当前原游戏图；不能把旧 NR 结果贴给新帧。异常降级在 UI/F8 显示原因并提示上传 BUG。

## 4. 私有 SR 重审结论：保留资产，升级路线与验证

### 4.1 当前两个 PR 各自是什么

#258 的最新提交已增加 D3D12 dispatch backend，不能再称为纯公式或空壳；但没有连上当前 Core 的真实 NR 输出、游戏接缝、配置和运行资源。保留其深度拒绝、帧配对、完整正负编辑和 fence 所有权资产。

#259 仍没有私有 NVIDIA Feature1 的 Create/Evaluate 后端。它实现的是 `EncodeCS/ComposeCS`、固定范围仿射载体与 FP32 尾差。不能通过增加一个菜单开关宣布功能已完成，也不能把 #258 中脚本提到的 deferred 后端文件当作 #259 已有文件。

两者共同的五份文件应合并为一份共享模块；继承 D21 当前 source-frame/descriptor/lifetime 设施，不能再用一套并行计数器代表主链所有权。

### 4.2 三条路径必须分清

符号：R=游戏真实渲染网格，N=NR 工作网格，O=重建输出网格。F=当前 NR 完整图链，U=私有 SR，Q=同一语义下的降采样，E/D=载体编解码。

**P：后置修改层放大，本版私有 SR 第一交付。**

`游戏原生 SR/RR→X_O；x_N=Q(X_O)；y_N=F(x_N)；delta_N=y_N-x_N；U(E(delta_N))→O；解码、统一合成到 X_O`。

这条 NR 看的是已经重建的完整图，适合先验证人脸和 RR 兼容。正常全尺寸 NR 不额外启动私有 SR。载体域可选模型代理线性或 scene-linear，但同一路径不得混减；已有光色系数只应用一次。

**B：前置生成、后置应用，继承 #259。**

`原始 X_R 的私有副本→F/现有N→R恢复→E_R；主 SR/RR 消费未改动 X_R；私有 SR 放大 (E_R-X_R)；同源后接缝合成`。

最小闭环沿 #259 的 R→O，而不是未经测试直接跳成 Delta_N→O。代价与降采样后置 P 分开计量；不许对原生结果再偷偷运行完整后置 NR。前置 R 的 jitter 不能照搬后置路径的零相位。

**RRC：跨 RR 编辑与累积，继承 #258，高级实验。**

保留游戏 RR 完整输入及一次调用，独立携带编辑；缺相机/前帧深度证据时不伪造深度拒绝，改走当前编辑或已验证光流分支。RR 输入带噪，不能宣称一定优于同 N 的重建后 P。

P/B 共用私有 SR backend；RRC 复用资源代次、帧身份和颜色契约，只有显式选择私有 SR 恢复时才使用该后端。不同意义的历史不能混用，也不默认连成三套时域过滤器。

### 4.3 本轮确认的数值与算法缺口

1. #259 `B=4` 是试验值。仿射载体写 FP16 后，正负 `1e-4` 均舍入到0.5，解码为0；tail=delta-clamp(delta)为0，不能补回范围内的舍入误差。现有随机测试是 FLOAT32 往返，没有覆盖这个存储步骤。
2. 较小固定范围提高小编辑精度，但会增加落入空间尾差的内容；wilsjo2 的 `k=1/64` 有理映射保住小编辑，却仍有端点逆映射误差风险。保留旧 B4 作回归反例；比较小范围仿射+tail与归一化有理映射，不把任一数字直接设为全游戏默认。
3. #258 把历史夹到当前3×3编辑范围。当当前邻域全部为0时，即使运动正确，历史也立即归零。它不是 Magpie 累积+的“出现/消退支持度”。复用基础设施，不把这段函数改名为累积+。
4. GPU 中性帧与零编辑必须单独验证。CPU编码为0.5不证明时域 DLSS 保持0.5；用户关闭/总贡献0用真实无编辑路径，不依赖模型猜中性。
5. 同尺寸双路 SR 差分 `U(P+delta)-U(P)`只作为诊断基准：DLSS非线性且两份历史独立，不等于U(delta)，也不等于两次误差完全相消；它增加 SR/VRAM 成本，不做默认预算方案。
6. 早期scene-linear编辑不能直接加到Present PQ/sRGB；一般 T(X+delta)-T(X) 不等于 delta。优先在匹配的重建输出域完成 B，晚期迁移另证。

### 4.4 私有 SR backend 的交付条件

独立 Feature1、参数块、Create 就绪、实际队列、输入输出大小和完整元数据；排除自身 hook 递归与错误原生源识别。原生 SR/RR 恰好一次，用户配置不做临时全局改写。

固定 carrier 不启用追踪其灰色分布的自动曝光，不默认开启 SR 锐化；仍须按实际运行库能力设置。P 的零 jitter 是已重建网格策略，B 的 jitter按真实输入配对。以源帧时间而非FG显示次数推进历史。

私有SR不可用且本帧NR候选安全有效时，可退到同帧空间恢复并提示，不停掉基础NR；若已发生不透明GPU故障则隔离该工作，不能在可疑list继续尝试第二套GPU路径。

## 5. 六个工作包与合并顺序

| 包 | 关键修改范围 | 完成出口 | 关联 |
|---|---|---|---|
| W0 运行基础与成本 | 上下文/父子层/recording代次、Present取消、关闭态、实际成本记录 | 反复位置/尺度/层数切换后有界收敛；未完成fence负例；相同实际工作量比较 | #9 #234 #272 #273 #277 |
| W1 兼容输入闭环 | nr_format_access及真实调用、合法canonical转换/写回、可选曝光、源与guide选择、独立Present、API transport | 真正无导引NR输出；一份有原生、一份无原生的DX12样本与DX11闭环；VK/legacy配方分别记录 | #2 #93 #202 #228 #229 #248 #239 #164 #190 |
| W2 颜色/质感/控件 | nr_color_policy、nr_color_amplification、实际compositor、光影/区域/肤色UI | 耦合/独立消费者分别验证，0/1x身份保持；raw-vs-final定位；总饱和度与有效状态可见 | #274 #104 #103 #71 #109 #208 |
| W3 共享光流防闪 | #250/#256可复用后端、源帧对、后续层差分、累积/+、低频模式 | AMDOF→稳定器→真实输出；关闭零新增光流成本；无历史直通；保留肤质小纹理 | #275 #239 |
| W4 私有SR与RRcarry | 共享dispatch/帧合同、真实Feature1、P/B选择、载体精度；#258高级接缝 | P先闭环，B复用后端，RRC独立验收；SR/光流各开关的2×2对照，不叠双NR | #259 #258 #104 #15 |
| W5 产品/部署与验收 | 双/三层预算预设、一次锐化、高级分层/饱和度、Manager配方、F8与回滚 | 用户无须拼插件；保留HoYoShade等已有外置部署；每项显示实际生效与重启边界 | #163 #165 #179 #191 #275 |

W0、W1、W2按互不覆盖的文件/接口并行；W3和W4可并行接在已具备的输入基础上。先用单层接通私有SR，再接现有完整MultiNR，不等待全部API实现。W5的组件清单/UI可并行，最终配方只承诺本项目实测过的组合。

这是同一大版本六个可回滚实现PR的范围，不要求先完成一个全新引擎。小修复应尽早提交，不需要等完整计划所有项都完工才交首个可测包。

### 5.1 预设与简单菜单

主菜单：位置（原有选择）、效果（单层/双层均衡/三层增强/自定义）、总效果量、防闪（关/累积/累积+/低频）。光流后端默认AMD、NVIDIA可选，进高级；真实原生MV优先策略不变。

双/三层起点比较主层100%+辅助75%，再扫67%和100%；两个比例同时显示对应实际像素。比例相对主层，不悄悄改游戏真实R。低分预设自动温和CAS类锐化，尊重用户关闭，只在防闪后合适的颜色/相位边界做一次；不逐层锐化。

高级保留第一层效果、后续层新增效果、大范围光影/细纹理、总饱和度与亮/暗区饱和度。原图降权不是无损，默认原图锚不削弱。分区基准固定，不随增强结果自激反馈。无效/互斥控件置灰并注明实际路径。

私有SR恢复方式单独在高级选择，不在预设点击时偷偷启用；初次比较同层数、同N、同效果量。只有测出净收益后才讨论进入推荐预设。

## 6. 验收压缩成一套共同矩阵

无需每个模块重复组织一整轮游戏测试；共享相同源帧捕获、模型/配置与场景，分阶段观察并做整版回归。

**正确性门槛**：全关闭与零贡献保留原游戏；一源一NR链、一原生SR/RR调用、一个FG presenter；初始化/重建/resize/切HDR/退出负例；GPU在途对象不可复用；失败保持本帧原图，反馈提示不得静默。

**画质矩阵**：单层参考、双/三层空间恢复、AMDOF基础/+、低频、私有SR开/关；人脸运动、暗部、草/栅栏、火焰/反射、HUD、遮挡显露、镜切。先无锐化定位，再加最终锐化；同时报告新增效果量，禁止仅靠抹掉NR提高稳定指标。

**成本矩阵**：NR1/后层、guide/OF、privateSR、尾差/合成、传输等待、总源帧P95/P99和源帧年龄；本机纹理/模型估算/退休/quarantine分别记。比较D13与D21须同层数同N；waiting-carrier文字不能独自证明GPU被堵。

**兼容矩阵**：DX12原生SDR/HDR、无原生且无depth、DX11 x64、Vulkan选定配方、一个DX9/32位配方；没有样本的API标“候选/未验证”，不继承外部项目完整支持列表。拒绝保护内容的合法限制不通过规避处理。

**私有SR矩阵**：binary16量化、正负微小编辑、0.5中性、黑白极值、alpha/预曝光、输出-only尺寸变化、原生jitter与post去jitter、强编辑tail、网格与鬼影；实际model/Create/Evaluate可用性独立于CPU数学。

Source-level / portable CPU / Windows compile / CI / WARP / NVIDIA / game分别登记。没启动CI不是通过。二进制版本、来源、运行库与模型hash只记录身份，不能用随意白名单替代能力判断。

## 7. PR处置与本轮实际交付

- #257：保留，按D21和#278的新调度关系移植有效增量，健康Present不周期打断，用户手选保持；不直接清旧claims。
- #258：保留独立高级跨RR目标；提取可复用dispatch/帧合同进入W4，修改历史策略标签；不宣称已接游戏。
- #259：本版优先继续，但升级为共享privateSR backend+P/B两种输入，不直接合并旧稿即算完成。补FP16与真实SR测试后再定载体。
- #250/#256：复用已有光流与跨API工作；逐项审查对D21的依赖，不能为导入一个后端整包覆盖D21。
- #278：菜单暂停与渲染缺导引是不同概念；保守/猜测仍分开，无数据不自动关NR；合并前验证源帧与恢复历史。
- #276：本轮只追加本计划及`tests/private_sr_storage_audit.py`，沿原研究分支，不另开重复研究PR。
- 旧PR在替代实现可追溯合入并覆盖测试前不关闭；不更换main、不强推、不自动发布OTA、不改当前游戏包。

本轮新验证：Python二进制16存储模拟39项检查；通过Git blob匹配的真实标量header，GCC/Clang各验证14个pack/unpack样本及零邻域历史裁剪。`--require-small-edit-survival`预期退出1表示旧B4设计问题仍在，不是运行时修复通过。新增脚本不加入自动CI，未运行shader、私有SR或游戏。

复现（输出写到工作构建目录）：

```text
python tests/private_sr_storage_audit.py
python tests/private_sr_storage_audit.py --scalar-header <PR259-checkout>/shaders/experiments/nr_edit_scalar.hlsli --cxx g++ --output <build>/storage.json
python tests/private_sr_storage_audit.py --require-small-edit-survival
```

## 8. 一手来源索引

- 本仓：#257/#258/#259完整变更及关键HEAD文件；D21 `src/interop/nr_format_access.h`；`src/nr_before_sr.cpp`；#228及其评论；#276/#278状态。
- [RenoDX资源升级表](https://github.com/clshortfuse/renodx/blob/e5d66cb/src/utils/resource.hpp)，[实际升级视图分支](https://github.com/clshortfuse/renodx/blob/e5d66cb/src/utils/resource_upgrade.hpp)，[slDLSSSetOptions作用域](https://github.com/clshortfuse/renodx/blob/e5d66cb/src/utils/dlss/streamline_v2.hpp)。
- [wilsjo2 v0.8.4](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/releases/tag/v0.8.4)，[后置私有SR实现](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8802b2b470db0462fa1ed03a125e793a7c06d735/OptiScaler/shaders/dlssnr/DlssNr_Dx12_Enlarge.cpp)，[已知限制](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8802b2b470db0462fa1ed03a125e793a7c06d735/docs/NR-DLSS-ENLARGEMENT.md)。
- [Magpie多层](https://github.com/SAOG0721/Magpie/blob/3841698348bfb246623d4acf791984c8b68a577b/src/Magpie.Core/DLSSNRMultiPass.h)，[时域shader](https://github.com/SAOG0721/Magpie/blob/3841698348bfb246623d4acf791984c8b68a577b/src/Magpie.Core/DLSSNRTemporalShader.h)。
- [Microsoft CopyResource](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copyresource)，[typed UAV能力](https://learn.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads)，[资源屏障](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)。

许可按实际文件、依赖与分发方式审查；公共库公式可参考不意味着SF二进制/模型或其他GPL组件可整体复制。本文没有复制供应商二进制、SDK实现或更改许可证。
