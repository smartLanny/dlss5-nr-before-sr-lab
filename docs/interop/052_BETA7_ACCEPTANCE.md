# Beta 7 验收与交付记录

**维护者测试候选已完成并部署异环；游戏验收待维护者安排。** 编译及包装均为 `80dcc99952b9cdd277d9780b292a2a169a5ff83f`；未启动游戏。完整身份、离线 ZIP、安装与回退见 [交付清单](052_BETA7_DELIVERY.json)，操作见 [手测清单](052_BETA7_MANUAL_CHECKLIST.md)。相对 D21 更新说明：[中文](052_BETA7_CHANGELOG.zh-CN.md) / [English](052_BETA7_CHANGELOG.en.md)。唯一实施路线仍为 [Beta 6 评估及后续路线](052_BETA6_EVALUATION_AND_NEXT.md)。

## 最终冻结候选的验证

[软件总收据](D:/ChatGPT/DLSS5/evidence/beta7-software-validation-80dcc999.json)绑定最终源代码、生成 Core、双语二进制及每项独立证据。以下全部属于最终 `80dcc999`，历史中间构建不改名为最终证据。

| 检查 | 最终结果与范围 |
|---|---|
| 完整统一双语构建 | [canonical exit 0](D:/ChatGPT/DLSS5/evidence/beta7-canonical-80dcc999-r1.json)，继承门槛及新门槛全部通过；双语静态分析零诊断，编译器已有警告保留 |
| CPU/WARP/控制器 | 严格残差 81 项、16,901,711 检查；生成时域 WARP 168 帧；控制器 99 项、策略 19 项；最终 Core WARP 17/17 模式通过 |
| 最终生成 Core 硬件 | [四模式](D:/ChatGPT/DLSS5/evidence/beta7-final-validation-80-r1/core-hardware/validation.json)全部通过；创建、提交、完成与退休独立核对 |
| 私有 Feature1 受控质量 | [最终 16 帧真实采集](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/exactcore-80-hair-real-r1/validation.json)通过，1173 检查/36 次推理；112 个原始平面及元数据与已分析 a2 完全相同，核对分析脚本及分析结果哈希后复用图审 |
| 后置原生 RR | [25/25](F:/ChatGPT/DLSS5/evidence/beta7-final-post-rr-80dcc999-r1/validation.json)：RR-only、1/3/5 层普通/私有 SR 的 50%/25%，AMDOF 自适应/低频全链与真实 P1 后续层；原生 MV 按合同拒绝 |
| 原生 SR 独立报告 | [两层/25%/奇数尺寸 2/2](F:/ChatGPT/DLSS5/evidence/beta7-final-sr-odd-80dcc999-r1/validation.json)；两支原始 B/Z 不同，只报告各自绝对成本，不作成对画质/成本因果归因 |
| 连续帧时域 | [最终 Core 672/672](D:/ChatGPT/DLSS5/evidence/beta7-final-validation-80-r1/temporal-hardware/result.json)，原生 MV 与 AMDOF、off/自适应/低频、全链/后续层分开；功能通过不等于全面去噪/无拖影 |
| 4K 完整产品时域链路 | [真实 RR→一层 NR 50%→PostRr AMDOF](D:/ChatGPT/DLSS5/evidence/beta7-final-validation-80-r1/product-4k/validation.json)，32 输出，实际创建/录制/提交/完成/退休通过；时域资源 394,592,256 字节（376.3125 MiB），保留 768 MiB 上限 |
| ReShade SR/RR | 两场最终 Core 私有 Feature1 生命周期通过，均为 0 层 NR；不与原生接口多层 Feature18 合并成游戏全链证明 |
| 离线包 | [中英文实际 Manager 原始解析器](D:/ChatGPT/DLSS5/evidence/beta7-ota-final-manager-80dcc999-r1/validation.json)通过；未操作 Manager UI、未修改默认 pin、未在线发布 |

私有 SR 同输入门槛以 [发丝 v2 对照](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/hair-comparison-r1.json)为最终依据：发丝 MAE 0.007088→0.005700（16/16 改善），P95 0.018092→0.00858，最差帧连续段 12→95/100 像素，帧间步差 0.004026→0.00145；平均对比轻降约 0.000029。文字和暗背景误差改善，零修改漏色为零；暗线 11/16 帧改善、5/16 轻微退步，同档普通恢复的发丝 MAE 约 0.00262 仍更好。因此只判定相对 Beta 6 旧实现的受控改善，不宣称普遍优于普通恢复或游戏已经达标。

时域连续帧显示静止/平移噪声与暗部误差改善；运动线中心对比有轻微负偏，五像素能量保留至少 99.625%。切镜精确 reset。显露带和反射没有普遍去噪收益，有限帧图审不证明普遍无拖影；`temporal_full_quality_pass=false`，继续保持手动实验身份。

4K 的历史计数是每 8×8 tile 抽取一个实际像素的 **GPU 抽样决策**：tested 3,369,600、adopted 3,369,570、rejected 30，不能当作全图像素采用率。22 个稳态样本的 temporal Record 区间中位数 0.852960 ms、P95 1.366944 ms；不含原生 RR、NR、私有 SR，不是整插件或整帧成本。RR 普通/私有的六组同工作量 B/Z 相同，局部区间比较见 [独立成本记录](D:/ChatGPT/DLSS5/evidence/beta7-offline-host-analysis-80-r1/rr_paired_cost.csv)。RR 夹具末帧只有平滑渐变，不能证明游戏细节、降噪或拖影质量。

以下保存实施过程中的证据与失败，带旧提交或“当时待完成”的项均为历史阶段记录；当前状态以前述最终回执为准。

## 历史阶段证据

| 项目 | 已验证范围 | 结果与限制 |
|---|---|---|
| 奇数四分之一工作网格 | CPU/shader 统一 floor 合同，257×193 → 64×48，低于下限仍拒绝 | WARP 残差 81 项、16,901,711 次严格检查通过；不能解释 Beta 6 偶数输出的已有颗粒 |
| 同帧私有 SR 归因 | 16 帧 B、Z、载体、N 尾差、重建及最终合成；独立 CPU 参考和 GPU 精确双线性 | 范围 1/4 的亮部与暗部 B/Z/depth、来源和 reset 身份相同；真实 Feature1 与精确双线性分别记录 |
| 编码范围探索 | 保持输入相同，仅改变测试夹具 carrier range | 选择范围 1＋旧范围 4 精确中性保留＋同像素零修改保真；范围 0.25 因削弱细线未采用。共享 RR/deferred 编码不变 |
| 连续帧时域 | 实际生产 shader，新增真暗部后 168 帧 WARP；独立硬件 672 帧，含原生 MV/AMDOF、自适应/低频及全链/后续层 | 新增暗部 8/8 指标改善；静止/平移细线误差 10/16 改善，仍有局部退步。运动线五像素能量保留至少 99.625%，不夸大单像素指标为可见模糊 |
| 遮挡、物体、HUD、反射 | 独立连续帧及同帧 off 对照 | 显露区域未增加拖尾但未去噪；HUD 指标改善，反射区域未改善；不等于游戏结果 |
| 后置 RR 准入 | 原生 MV 明确拒绝；AMDOF/低频的全链/后续层夹具已纳入 | CPU 路由检查 24/24；d1 Core 真实原生 RR→一层 Feature18→全链 AMDOF 已完成 32/32 提交，历史采用 842,396 个 GPU 抽样决策；当时后续层独立验证中，最终矩阵已通过 |
| 一键 GPU 采样 | 九组 180 有效样本预热/600 样本计时/三张截图；20/60 秒上限 | d1 完整生成 Core 控制器 99 项、策略 19 项通过；当时最终新 Core 统一门槛待复验，现已通过 |

局部时域细线退步和未改善区域保留为反证，不做“已解决降噪/拖影”的结论。外部累积与私有 SR 保持互斥；未增加默认开启的保护。

## 证据身份与失败记录

- 首个冻结实施提交：`51c3a832a49131825d39f195223b248ce99b1c3b`。
- [完整统一构建首轮回执](D:/ChatGPT/DLSS5/evidence/beta7-canonical-51c3a832-r1.json)：失败，生成计时插桩的目标锚点出现两处，尚未进入完整 Core 编译；[原日志](D:/ChatGPT/DLSS5/evidence/beta7-canonical-51c3a832-r1.log)保留。
- [范围 1 WARP](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/quality-range1-r1/validation.json)、[范围 4 WARP](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/quality-range4-r1/validation.json)、[同输入证明](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/range1-vs-range4-input-identity.json)。这些是独立模块证据，不能代替完整生成 Core。
- [连续帧 WARP 回执](D:/ChatGPT/DLSS5/evidence/beta7-temporal-quality-20260928/sequence-warp-r4/result.json)、[逐帧指标](D:/ChatGPT/DLSS5/evidence/beta7-temporal-quality-20260928/sequence-warp-r4/analysis.json)。新增暗部 [WARP 168 帧](D:/ChatGPT/DLSS5/evidence/beta7-temporal-quality-20260928/sequence-warp-dark-r1/result.json)、[真实 RTX 5090 D 672 帧](D:/ChatGPT/DLSS5/evidence/beta7-temporal-quality-20260928/sequence-hardware-dark-r1/result.json)通过功能检查，原生 MV/AMDOF 各 336 帧，采用/拒绝与完成计数一致。硬件暗部平坦、低幅编辑、细线三类指标均 8/8 改善；静止/平移细线仍仅 10/16 改善，显露带与闪变反射沿用当前帧而未降噪。该独立模块结果不代替真实原生 RR 全链或完整生成 Core。
- 正式双轴审查固定为 `ad47a5a9..51c3a832`：未发现 P0/P1；两个 P2 已修：保留组起点采用计数，成功 Reset 按精确 token 清理未提交计时。定向源码复核通过，新增控制器回归当时待生成 Core 运行，最终 99 项已通过。

- [首轮真实 Feature1 范围 4](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/quality-real-range4-r1/validation.json)、[范围 1](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/quality-real-range1-r1/validation.json)：各 16 同输入采集帧及原始生命周期检查通过。精确空间参考的全区平均绝对误差为 0.005019 → 0.001485；固定输入帧间平均变化为 0.002475 → 0.000552。它们属于独立模块研究，尚不满足完整暗部/普通恢复与候选交付要求。
- [6sol 独立图片审核](D:/ChatGPT/DLSS5/evidence/beta7-independent-image-review-20260928/review-manifest.json)：确认细线局部退步及原素材缺少真暗部/真实文字，已据此增强受控夹具；未用图像观感替代同源数值证明。

## 私有 SR 最小修正与新增实际证据

范围单独从 4 改为 1 曾使严格微量回归失败，失败保留，未放宽容差。最终只在 post 残差编码中保留旧范围 4 的精确中性通道，小修改完整保存在原 N 网格尾差中；合成时仅在同像素有限 B 与当前 Z.rgb 完全相等时保留 B。未把 O 网格高频差加回。RGBA16 typed UAV load 能力在创建时缓存，缺失仅拒绝 PostCompose，不禁用其他 kernel。

- [真实暗部范围及门控对照](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/post-only-candidate-quality-report-r1.json)：相对旧范围 4，暗线 MAE 0.006594→0.006031、P95 0.018549→0.008837，文字 MAE 0.014375→0.003675，暗背景 MAE 0.009176→0.003327；固定输入暗背景帧间变化 0.004415→0.000884，零修改区域漏色严格为零。暗线 4/16 帧的 MAE 轻微回退，整体 P95 与对比度改善。
- 同档普通恢复保留独立对照：其暗线误差约 0.004，优于私有 SR 约 0.006；文字误差约 0.004271，劣于私有 SR 约 0.003675。**不宣称私有 SR 普遍优于普通恢复**。范围 0.25 虽更平滑，却损伤细线对比，未采用。
- [集成源码严格 WARP](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/post-integrated-warp-r1/validation.json)、[七平面逐字节闭环](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/post-integrated-contract-check-r1.json)：共享编码 helper 不变，最终 post 实现与已测实验的全部 16 帧七平面相同；当时完整 Core 待重建，最终统一构建已通过。
- [4K、768 MiB 上限实测](D:/ChatGPT/DLSS5/evidence/beta7-temporal-4k-budget-20260928/run-r1/validation.json)：d1 生成模块 40/40 帧创建/录制/提交，GPU 抽样历史决策采用 4,224,977 / 4,406,400，实际时域资源 376.3125 MiB，退休队列归零。32 稳态样本的时域增加中位数 0.849264 ms、P95 1.063712 ms；该模块夹具不证明完整产品剩余预算准入，也不是游戏整帧 ≤3%。
- [d1 生成 Core 控制器](D:/ChatGPT/DLSS5/evidence/beta7-acceptance-controller-d1bc2e0a-r1/validation.json)：99 项控制器及 19 项策略通过。
- [真实后置 RR 全链冒烟](D:/ChatGPT/DLSS5/evidence/beta7-rr-temporal-smoke-d1-r1/validation.json)：一层、50% NR、AMDOF 实际历史采用；22 稳态区间样本中位数 0.288256 ms、P95 0.291584 ms，未合成为总帧时间。

第二次完整构建 `d1bc2e0aa5b09013a9fe54845dd86b2b5293bfde` 的双语 Core 与继承门槛完成，但新连续帧门槛将仓库测试脚手架误当作生成产品源而失败：[原回执](D:/ChatGPT/DLSS5/evidence/beta7-canonical-d1bc2e0a-r1.json)。已修正来源映射，明确区分生成模块和仓库脚手架；[相同 d1 Core 定向复验](D:/ChatGPT/DLSS5/evidence/beta7-temporal-quality-20260928/sequence-generated-core-warp-dark-r1/result.json)为 168/168，初始化失败也保存失败收据。旧失败未覆盖，最终统一构建须重新通过。

## 发丝专项与 a2 实际 Core 预检

[发丝 v2 真实同源对照](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/hair-comparison-r1.json)为 `passed`：旧范围 4 原始编码/无门控与新范围 1/精确中性保留/零修改保真使用相同 16 帧 B/Z/depth，输入字节逐帧相同。发丝 MAE 0.007088→0.005700（16/16 帧改善），P95 0.018092→0.00858；最差帧超过源半对比阈值的最长连续段 12→95/100 像素，帧间步差 0.004026→0.00145。平均发丝对比轻降约 0.000029；同档普通空间恢复 MAE 约 0.00262 仍优于私有 SR。6sol 已按原像素检查曲线、文字与暗部，未声称私有 SR 普遍优于普通恢复。

第三次统一构建冻结为 `a2afcc4fa9c507907764b2115707fa343a7c2502`。双语静态分析零诊断，完整 Core、继承门槛、预算和生成时域 WARP 168 帧通过；新增私有质量门槛缺测试专用 RR/deferred 头而失败：[原构建回执](D:/ChatGPT/DLSS5/evidence/beta7-canonical-a2afcc4f-r1.json)。只修验证脚本的来源映射，产品源码、五对象和八 shader 仍锁定生成模块，测试专用头单列仓库脚手架来源；[相同 a2 Core 定向回执](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/exactcore-a2-hair-warp-r3/validation.json)严格 81 项、微量同档、16 帧发丝与分析通过。

[余下统一门槛预检](D:/ChatGPT/DLSS5/evidence/beta7-postbuild-preflight-a2afcc4f-r1/validation.json)全部通过，含导引、错误恢复负例、时域路由、辅助 shader、原生输出与重建。[完整 Core 控制器](F:/ChatGPT/DLSS5/evidence/beta7-acceptance-controller-a2afcc4f-r1/validation.json)为 99 项、策略 19 项。独立 Core WARP numeric 发现假 Feature1 夹具仍按旧范围 4 填 carrier，而产品已经是范围 1，0.25 编辑被假输入解码为 0.0625；原失败保留，只更新夹具数学输入，逐像素、边界和 alpha 精确断言不放宽。修正后的最终 Core WARP 17/17 与 `80dcc999` 干净源码统一构建现已通过。

## 报告范围

一键报告测量同一 Direct 队列、同一完成栅栏所覆盖的原生调用前后 Core 区间；原生 SR/RR 时间不包含在内。跨队列或覆盖不足时不提供总插件/整帧成本。原生无 NR 组没有该插件 GPU 工作，记为不适用并保留截图，不伪造 GPU 样本。创建、reset、旁路、切换和截图排除；有效不足和不支持均明确记录并继续；结束/取消恢复配置，不写入 INI。

后置原生 RR 的无 NR、普通 NR、私有 Feature1 和 AMDOF/低频分别验证。SR/RR 报告按当前游戏原生模式区分，由维护者切换游戏选项；工具不切换。原生 MV 未获同源导引证明时继续拒绝。私有 Feature13 RR、跨 RR 修改量及 NVOF 不混入本轮通过项。

## 交付完成与维护者待验收

软件与受控私有 SR 改善门槛已闭环，中英文包和独立安装回执见 [交付清单](052_BETA7_DELIVERY.json)。6sol 已确认异环退出后仅替换中文 Core，在插件搜索目录之外备份 Beta 6，核对版本、完整提交、哈希、单一加载位置及运行库/配置字节。

剩余验收是游戏代表场景的噪点、闪烁、拖尾、模糊、发丝/暗部/文字，以及同 NR 工作量整游戏帧新增成本 ≤3%；超预算但有效的继续只作手动实验。由维护者先异环、2077 最后测试，原生 SR/RR 分开设置。保留 Esc/切镜、Present、地图省电和多层暗部检查；不启动游戏、不合 main、不发布在线 OTA、不改 Manager 默认版本。
