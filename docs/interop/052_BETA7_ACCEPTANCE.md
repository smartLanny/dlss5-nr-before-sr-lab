# Beta 7 开发验收记录

状态：**开发中，未交付；当前安装仍为 Beta 6。** 私有 SR 的真实 Feature1 受控改善尚未证明，不以功能录制成功、WARP 结果或新测量工具替代画质门槛。唯一实施路线见 [Beta 6 评估及后续路线](052_BETA6_EVALUATION_AND_NEXT.md)。

相对 D21 更新说明：[中文](052_BETA7_CHANGELOG.zh-CN.md) / [English](052_BETA7_CHANGELOG.en.md)，均保留开发身份。

## 本轮已取得的证据

| 项目 | 已验证范围 | 结果与限制 |
|---|---|---|
| 奇数四分之一工作网格 | CPU/shader 统一 floor 合同，257×193 → 64×48，低于下限仍拒绝 | WARP 残差 61 项通过；不能解释 Beta 6 偶数输出的已有颗粒 |
| 同帧私有 SR 归因 | 16 帧 B、Z、载体、N 尾差、重建及最终合成；独立 CPU 参考和 GPU 精确双线性 | 范围 1/4 两组 B/Z/depth、来源和 reset 身份相同；首轮真实 Feature1 两组通过功能运行，新增暗部夹具仍待验证 |
| 编码范围探索 | 保持输入相同，仅改变测试夹具 carrier range | 范围 1 在首轮真实 Feature1 中也减少偏差和固定输入帧间波动；原素材没有真正暗部，生产默认仍为 4 |
| 连续帧时域 | 实际生产 shader，新增真暗部后 168 帧 WARP；独立硬件 672 帧，含原生 MV/AMDOF、自适应/低频及全链/后续层 | 静止/平移末帧 8/8 噪声误差降低，细线误差仅 4/8 改善，仍有局部退步；4/4 切镜重置精确 |
| 遮挡、物体、HUD、反射 | 独立连续帧及同帧 off 对照 | 显露区域未增加拖尾但未去噪；HUD 指标改善，反射区域未改善；不等于游戏结果 |
| 后置 RR 准入 | 原生 MV 明确拒绝；AMDOF/低频的全链/后续层夹具已纳入 | CPU 路由检查 24/24；真实原生 RR、Feature18 与时域全链仍待显卡验证 |
| 一键 GPU 采样 | 九组 180 有效样本预热/600 样本计时/三张截图；20/60 秒上限 | 当前为源码与独立策略验证，完整生成 Core 验证待完成 |

局部时域细线退步和未改善区域保留为反证，不做“已解决降噪/拖影”的结论。外部累积与私有 SR 保持互斥；未增加默认开启的保护。

## 证据身份与失败记录

- 首个冻结实施提交：`51c3a832a49131825d39f195223b248ce99b1c3b`。
- [完整统一构建首轮回执](D:/ChatGPT/DLSS5/evidence/beta7-canonical-51c3a832-r1.json)：失败，生成计时插桩的目标锚点出现两处，尚未进入完整 Core 编译；[原日志](D:/ChatGPT/DLSS5/evidence/beta7-canonical-51c3a832-r1.log)保留。
- [范围 1 WARP](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/quality-range1-r1/validation.json)、[范围 4 WARP](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/quality-range4-r1/validation.json)、[同输入证明](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/range1-vs-range4-input-identity.json)。这些是独立模块证据，不能代替完整生成 Core。
- [连续帧 WARP 回执](D:/ChatGPT/DLSS5/evidence/beta7-temporal-quality-20260928/sequence-warp-r4/result.json)、[逐帧指标](D:/ChatGPT/DLSS5/evidence/beta7-temporal-quality-20260928/sequence-warp-r4/analysis.json)。新增暗部 [WARP 168 帧](D:/ChatGPT/DLSS5/evidence/beta7-temporal-quality-20260928/sequence-warp-dark-r1/result.json)、[真实 RTX 5090 D 672 帧](D:/ChatGPT/DLSS5/evidence/beta7-temporal-quality-20260928/sequence-hardware-dark-r1/result.json)通过功能检查，原生 MV/AMDOF 各 336 帧，采用/拒绝与完成计数一致。硬件暗部平坦、低幅编辑、细线三类指标均 8/8 改善；静止/平移细线仍仅 10/16 改善，显露带与闪变反射沿用当前帧而未降噪。该独立模块结果不代替真实原生 RR 全链或完整生成 Core。
- 正式双轴审查固定为 `ad47a5a9..51c3a832`：未发现 P0/P1；两个 P2 已修：保留组起点采用计数，成功 Reset 按精确 token 清理未提交计时。定向源码复核通过，新增控制器回归仍需生成 Core 运行。

- [首轮真实 Feature1 范围 4](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/quality-real-range4-r1/validation.json)、[范围 1](D:/ChatGPT/DLSS5/evidence/beta7-private-sr-quality-20260928/quality-real-range1-r1/validation.json)：各 16 同输入采集帧及原始生命周期检查通过。精确空间参考的全区平均绝对误差为 0.005019 → 0.001485；固定输入帧间平均变化为 0.002475 → 0.000552。它们属于独立模块研究，尚不满足完整暗部/普通恢复与候选交付要求。
- [6sol 独立图片审核](D:/ChatGPT/DLSS5/evidence/beta7-independent-image-review-20260928/review-manifest.json)：确认细线局部退步及原素材缺少真暗部/真实文字，已据此增强受控夹具；未用图像观感替代同源数值证明。

## 报告范围

一键报告测量同一 Direct 队列、同一完成栅栏所覆盖的原生调用前后 Core 区间；原生 SR/RR 时间不包含在内。跨队列或覆盖不足时不提供总插件/整帧成本。原生无 NR 组没有该插件 GPU 工作，记为不适用并保留截图，不伪造 GPU 样本。创建、reset、旁路、切换和截图排除；有效不足和不支持均明确记录并继续；结束/取消恢复配置，不写入 INI。

后置原生 RR 的无 NR、普通 NR、私有 Feature1 和 AMDOF/低频分别验证。SR/RR 报告按当前游戏原生模式区分，由维护者切换游戏选项；工具不切换。原生 MV 未获同源导引证明时继续拒绝。私有 Feature13 RR、跨 RR 修改量及 NVOF 不混入本轮通过项。

## 尚未通过的交付门槛

1. 同输入真实 Feature1 对照及较 Beta 6 的私有 SR 受控画质改善，含同档普通恢复、暗部、文字、发丝和细线。
2. 新源码完整双语统一构建、生成 Core 控制器/生命周期、Feature18/Feature1 和后置原生 RR/AMDOF 硬件回执。
3. 连续帧降噪/拖影独立判定和同 NR 工作量成本；游戏整帧新增成本 ≤3% 由维护者实测。
4. 通过后才建立正式包身份，并由 6sol 确认异环退出后备份、部署和核对；当前没有新包可交付。

游戏由维护者安排，先异环、2077 最后。保留 Esc/切镜、Present、地图省电、多层暗部验收。旧 Beta 6 配置、包和回退均保留；不启动游戏、不合 main、不发布在线 OTA、不改 Manager 默认版本。
