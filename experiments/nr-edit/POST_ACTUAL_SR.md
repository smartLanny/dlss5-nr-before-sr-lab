# 后置最终残差：真实模型输出的离线验证

## 结论与身份

2026-09-21，在独立实验分支完成实际 Feature18 捕获 → 显式共同转码 →
`Encode` → 一次实际私有 Feature1 SR → `Compose` 的离线数值检查。
这不是同一运行时内的 NR/SR 接线，也不是游戏输入：NR 的源图来自受控宿主。

生产捕获与宿主编译基底为 unified7 完整 r4：
`a627f28b6f81bf3404456582146695c25bc55927`。实验未加入该正式包，
未修改主 Core、菜单、颜色控制、传输算法或夹持规则。

三种零/微小编辑对照精确通过；五种真实 NR 像素样本都完成真实 SR 调用、
存储和加回检查，但**最终 SDR 域检查拒绝**。这是数值拒绝，不是运行失败。
记录的最终状态为 `completed-with-numerical-refusals`，各实际 NR 案例退出码 4；
不能把这个状态记为五例图像验收通过。

## 输入证明与显式人工部分

`prepare_post_capture.py` 只接受已完成、实际模型调用通过的 Present NRF 回执，
核对捕获/元数据/生产源码/运行时哈希及同帧、同实例、最终请求层身份。
捕获头中的源码标识可以是 `unknown`；本实验使用生产回执的源码哈希，
不会把该字段改写为已知提交或冒充游戏来源。

原始 `Source=B` 与最终 `Composite=Z` 都是 **LinearSdr FP16 全分网格**。
直接作为 `SdrEncodedFp16` 输入必须拒绝。只有明确选择离线研究后，才对 B/Z
共同施加 IEC sRGB RGB 编码、保留 alpha，再以最近偶数存成真实 FP16 字节；
记录前后哈希、量化误差、非有限值/域外值/裁剪计数。本批输入后三项均为零。
转码遇到越界或未知域就拒绝，不 saturate，不设置 `scene_linear_verified`。
SR 宿主运行前重新审核原始捕获回执并重新转码，要求生成包哈希完全相同。

NRF 中的颜色专用 depth/MV 占位值不作为原生导引。独立构造的 SR 夹具是
非反转硬件深度 0.5、工作网格像素单位的零 MV、jitter=0、unity exposure、
Reset=true；读回后逐值审核。这只声明人工静态平面，不声明捕获相位正确、
存在真实运动、遮挡或原生导引来源。SR 创建 flags=2、quality=0，使用正式
NGX owner 解析和独立参数/句柄，不执行第二次全局 Init。

## 实际输出与空间对照

真实 NR 捕获：640×360，工作网格 160×90，分别请求一至五层并选择最后一层。
只对最终 `Z-B` 做一次面积缩图及一次 SR，不按层重复增强。B 与 Z 分配为不同
资源；目标可复用 Z，但在 Encode 读取完成并转换状态后才写入。实际 fence、
不可重放 recording 退休和释放均检查；底图字节与 alpha 保持不变。

控件已包含在 Z 内。输出仅为 `B + reconstructed edit + spatial tail`，
不再次施加颜色或增强控制。CPU 使用实际 SR 输出与实际 tail 检查一次加回，
误差最多 0.0009765625；五例所有存储像素有限，SR 载体本身均在 [0,1]。

下表以 RGB **通道数**计数，分母每例 691,200。原始 B/Z 均无越界。
离线空间对照复用同一份 GPU 生成的低分 carrier/tail、范围 4 和半像素相位：
第一种直接双线性恢复残差；第二种额外将恢复载体存 FP16，再做相同加回。
对照使用 CPU 双精度中间运算和最近偶数 FP16 最终存储，未再调用模型，
也不冒充 GPU 对照运行。

| NR 层数 | 空间残差恢复越界 | 空间载体 FP16 越界 | 实际 SR 越界 | SR 相对 FP16 对照新增越界 | 实际最小/最大值 |
|---|---:|---:|---:|---:|---|
| 1 | 34 | 36 | 805 | 778 | −0.003284 / 1.008789 |
| 2 | 643 | 527 | 1,684 | 1,201 | −0.003590 / 1.011719 |
| 3 | 826 | 543 | 2,073 | 1,580 | −0.003227 / 1.011719 |
| 4 | 448 | 821 | 2,561 | 2,000 | −0.003571 / 1.010742 |
| 5 | 447 | 585 | 2,289 | 1,842 | −0.003288 / 1.010742 |

“新增越界”指相同位置的对照仍在域内、实际 SR 结果域外，不是两个总数相减。
两种空间对照均只有上越界；实际 SR 另产生 702/724/698/677/658 个负通道。
所以原始 NR 输出本身没有越界，空间恢复已经破坏部分边界，真实 SR 又增加
越界；不能把全部问题归因于某一个环节。FP16 存储也会改变边界计数。

三个 640×384 → 160×96 对照以 B=0.625、Z=B / B+1/1024 / B−1/2048 构造。
实际 SR 的中性输出最多偏离 0.5 达 0.0009765625，但支持标签阻止其进入
零/仅尾差编辑；三例最终图与 Z 逐字节相同，B 不变。

## 成本范围

本批五例单次 Reset Evaluate 的 GPU 时间：Extract 0.0071–0.0081 ms，
实际 SR 0.1263–0.1323 ms，Compose 0.0045–0.0046 ms。三个传输表面的
实际分配需求合计 2,359,296 字节（2.25 MiB）。

这是小分辨率、完成 Create 后的一次调用，非稳定性能基准；不包括 NR、
Create、上传/读回、B/Z、人工导引、描述符和驱动私有分配，不能外推游戏 FPS、
4K 成本或显存节省。首次编译失败及两次实际运行回执均保留。

## 复现与证据边界

```text
python experiments/nr-edit/tools/prepare_post_capture.py --capture <actual-final-layer.nrf> --validation <producer-validation.json> --output <fresh-pair-directory> --offline-static-planar
python experiments/nr-edit/tools/validate_post_residual_ngx.py --core <frozen-generated-core> --compiler <clang++.exe> --reshade <checkout> --nrchain <verified-bridge.dll> --sr-runtime <existing-signed-runtime.dll> --pair-proof <pair-proof.json> --output <fresh-private-directory>
python experiments/nr-edit/tools/analyze_post_residual_ngx.py --validation <actual-run/validation.json> --output <fresh-storage-audit.json>
python experiments/nr-edit/tests/test_post_capture.py
python experiments/nr-edit/tests/test_integration.py --generated-root <frozen-generated-core>
```

`--pair-proof` 可以重复。验证器总是加入三个受控对照。没有提供 NRF 时只验证
人工对照，不能报告真实 NR 端到端样本。原始捕获、DLL、编译输出、逐像素结果、
日志及绝对路径只存于私有证据；源码只包含宿主、工具与本脱敏说明。

私有回执标识：`post-real-sr-unified7-r1` 为旧 overlay 编译失败；
`r2` 为首次实际输出；`r3` 为完整独立输入复核后的最终结果和 `storage-audit.json`。
捕获准备标识 `post-real-unified7-input-r2`；门禁回归标识
`post-real-sr-regression-r1`。五项输入门禁、旧/新生成 Core 各八项 staging
回归通过；未显式选择人工离线几何时，实际 NRF 仍按预期退出 2 拒绝。
旧宿主只增加可被新宿主复用的 main 宏保护；新旧 DispatchCodec 签名分别保留。

## 下一步最小方向

先明确域外重建的拒绝语义：保留已经写入的有效 Z，而不是夹持新结果；在
独立实验中验证全帧或逐像素拒绝的边界、连续性、零编辑和微小正负编辑。
这需要新的输出合同，当前没有实现或偷偷扩大准入。仅更换私有 SR 不能解决
空间恢复自身已存在的越界；仅保留 FP32 量化尾差也不能消除模型输出偏移。
模型对有符号载体的响应校准应作为后续独立受控试验，不能由本批推导画质改善。

真实导引/相位、原生后置 SR/RR、HDR、移动序列/历史、所有 Core 25% 对齐尺寸、
游戏、时域边界及最终格式量化均未验收。严格实际尺寸 `work*4>=full` 保持不变；
例如 Core UI 25% 的 257→64 仍拒绝。不要把这批结果用作开启 Core 菜单的依据。
