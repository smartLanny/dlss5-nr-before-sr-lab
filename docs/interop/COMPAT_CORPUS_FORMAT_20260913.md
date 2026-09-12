# Format / view / exposure / color 语料（2026-09-13）

公开 OptiScaler / RenoDX / Dagherbou 历史对照 Family A。
只提取：症状 → 根合同 → 建议泛型原语 → 可能帮助的本仓库 issue。
不引入 GPL 树，不建议在原生 DLSS 路径强制挂 Opti 中间层，不加 exe 白名单。

审计日：2026-09-13。本仓库阅读起点 `codex/dx12-compat-multipass-20260912` @ `3c13f69119ddb0a0c1246f0348eb15c8aac64f76`。
OptiScaler 远端 HEAD（只作时间锚，不是算法许可）：`5ee53e38e6f012d8f6420301063a73e47c114f84`。
GitHub 对本私有仓库的 Issues API 返回 403（integration 只有 `metadata=read`）；issue 正文以本仓库已有 interop/testing 文档与公开上游为准，未把私有 inbox 路径写进本文件。

## 先看这 5 条

1. **#2 + #248 + #202 共用一个根：TYPELESS 存储必须用「当前代次、当前资源」的原生 typed view，而不是猜测表。** Opti `TranslateTypelessFormats`（仍在 `Shader_Dx12.cpp`，typeless 相关提交 `88c3f691c121d26de57333c80ea0ff01340a2ebd`）证明症状同类，但他们的 `R8G8B8A8_TYPELESS → UNORM` 猜测正好是我们合同禁止抄的东西。
2. **#177 是另一条根：HDR 编码 / 白点 / 残差回写，不是格式拒绝。** RenoDX 合成与 Dagherbou v0.1.2–v0.2.0 的 exposure-anchored white point 有用；Dawnwalker 的 NR 已经在跑。
3. **#228 / #229 的 R8_UNORM 曝光：上游是反面教材。** Opti 官方 workaround 是丢掉游戏曝光改 AutoExposure；Dagherbou `ExposureScan` 把 `fmt 61`（`R8_UNORM`）1×1 标成 filter rejected。我们已有 typed 归一化红通道读取，缺的是同候选实机，不是再抄他们的过滤器。
4. **#253 / #247 燕云 `output-color-contract-unverified`：公开名单帮不上忙。** Where Winds Meet wiki 只有 `dxgi.dll` / 在线风险，没有 LDR 编码证据。继续走我们自己的有界 R8 合同。
5. **Opti/Reno 明确帮不上：** #252（军团 F6/掉帧）、#68 的队列亲和部分、#247 旧版对照（不要和 #253 合成一个失败）。#228 的 R8 读取本身也不要再向他们要算法。

不要做的事：把 Opti 做成原生 DLSS 必经中间件；按游戏名加允许名单；把 TYPELESS 一律当成 UNORM；用 AutoExposure 冒充已读到游戏曝光。

## 两条共用根 → 两个泛型原语

| 根合同 | 共享 issue | 建议原语（本仓库已有骨架则写明） | 禁止抄的上游做法 |
| --- | --- | --- | --- |
| TYPELESS / 视图 / 代次 | #2 #248 #202 | `ObservedNativeView`（当前 Feature 代 + 资源生命周期）→ 仅当观察缺失且设备查询只剩 **一个** 合法解释时才 `UniqueContract`。已在 `ResourceSemanticResolver` | Opti `TranslateTypelessFormats` 猜测表；`R10G10B10A2_TYPELESS → UINT` |
| HDR 编码 / 白点 / 残差 | #177（#253 只借「编码身份」词汇，不借算法） | 验证过的编码身份（linear HDR / PQ / sRGB）+ transfer-matched residual + 可选的 **已读到** 曝光白点。NR 已成功时不要再走格式旁路 | AutoHDR / 反 tonemap；强制 `HDR_INPUT_COLOR`；把 RenoDX addon 嵌进原生路径 |

## 总表

| 我们的 issue | 上游命中？ | 合同维 | 可能有帮助？ | 建议下一步 |
| --- | --- | --- | --- | --- |
| #2 TYPELESS generic view | **是** Opti `TranslateTypelessFormats` + [commit 88c3f691](https://github.com/optiscaler/OptiScaler/commit/88c3f691c121d26de57333c80ea0ff01340a2ebd)（Typeless Hudless） | 存储 TYPELESS ≠ 视图；观察必须绑当前代次 | **Yes** | 收紧「当前代次观察」；禁止猜测表；#248/#202 走同一解析器 |
| #248 绝区零 unsupported-output-format | **部分** 同上泛型 TYPELESS；[ZZZ wiki](https://github.com/optiscaler/OptiScaler/wiki/Zenless-Zone-Zero) 只写 DX12 / `d3d12.dll` / FSR4 Non-Linear sRGB / 翻转 MV，**没有** Output 格式 | 后置 Output 视图 + Feature 重建后证据作废 | **Yes**（与 #2 同根） | 不要抄 ZZZ quirk；修「重建后旧 UNORM 观察不能继承」 |
| #202 看门狗：军团 format | **部分** 兼容表仅 `Watch Dogs: Legion ✅ DLSS`，无格式注记；同 TYPELESS 根 | 颜色视图 / LDR 曝光门禁 / 生命周期 | **Yes**（与 #2 同根） | 保持原 exe `WatchDogsLegion.exe`；只验收当前代次稳态拒绝是否还在 |
| #252 军团 F6 关仍掉帧 | **否** 无公开格式/编码命中 | 热路径 / 整帧，不是格式 | **No** | 与 #202 分轨；格式恢复成功也不关此单 |
| #253 燕云 output-color-unverified | **弱** [Where Winds Meet](https://github.com/optiscaler/OptiScaler/wiki/Compatibility-List) 仅 `dxgi.dll` + 在线风险 | 有界 LDR typed R8 输出编码未证实 | **Partial** | 继续本仓库 LDR 编码证据；不要用 RenoDX swapchain 预设冒充 NGX Output 合同 |
| #247 旧 0.4.7beta 燕云颜色拒绝 | **否** 与 #253 同症状、不同候选签名 | 历史对照 | **No** | 只对照签名/版本；不合并成 D15 失败 |
| #228 DS2 R8_UNORM ExposureTexture | **反面** [Opti Known Issues · Exposure](https://github.com/optiscaler/OptiScaler/wiki/Known-Issues)；Dagherbou [#41](https://github.com/Dagherbou/OptiScaler_DLSSNR/issues/41) 拒绝 `fmt 61` | 1×1 typed R8 归一化红通道 | **Partial** | 保留已实现 reader；不要 AutoExposure；R8 TYPELESS/UINT/SNORM 仍拒绝 |
| #229 DS2 前后置 + typed 曝光 | 同 #228 | 同上 + 存储量化 | **Partial** | 同候选 F8 看 NR/提交是否增长；与 DS1 后置消费者分轨 |
| #68 NBA 2K27 曝光/TYPELESS 角 | **否** 兼容表无 2K27 格式/曝光条 | 曝光合同只是早期可能阻断之一 | **No** | 只要曝光角：对照 #228 的 typed 读取是否真出现；队列亲和不在本家族 |
| #177 Dawnwalker HDR / feel-weak | **是** RenoDX 合成（MIT 归因已在本仓库 notices）；Dagherbou [v0.1.2 颜色映射](https://github.com/Dagherbou/OptiScaler_DLSSNR/issues/5) + v0.2.0 游戏曝光白点；[Dawnwalker wiki](https://github.com/optiscaler/OptiScaler/wiki/The-Blood-of-Dawnwalker) 只提强制 tonemapper sharpening | 编码身份 / 白点 / 残差可见性 | **Yes** | NR 已成功时做「验证过的 HDR 编码 + transfer-matched residual」；不要挂 Opti |

## 语料行

### C1 · TYPELESS → 猜测 typed view（Opti）

- **症状：** 游戏把 Color/Output/Hudless 建成 TYPELESS；中间层要 SRV/UAV/RTV。
- **上游：** `Shader_Dx12::TranslateTypelessFormats` 现仍存在（文件最近提交 `9c7348f678d18499e0e3a358c1c5b43698507a8f`，函数本身在 `88c3f691` 的 typeless Hudless 修复中被继续使用）。表：RGBA32/16/8 TYPELESS→FLOAT/UNORM，`R10G10B10A2_TYPELESS→UINT`，深度 TYPELESS→X24 读法。`CreateShaderResourceView` / UAV / RTV 都先跑这张表。Format-transfer 甚至把 UNORM 建成 TYPELESS 再译回来（`FT_Dx12.cpp`）。作者自己在深度分支写了 “I'm not 100% sure if it's fine”。
- **根合同：** 物理格式不是准入证据。NVIDIA DLSS 指南没有通用 `TYPELESS→UNORM` 表（见 `NATIVE_COLOR_VIEW_20260909.md`）。
- **建议原语：** 当前成功原生 Feature 1 调用上的 Color SRV / Output UAV；冲突/过期/跨代次 → 保留原生 SR。Unique contract 只有「声明合同 + 设备查询」把候选收到 1 个时才成立；**未实现的 codec 不得制造唯一性**。
- **可能帮助：** #2 #248 #202。
- **不抄：** 猜测表；把 Opti 插进原生 DLSS。

### C2 · 绝区零名单 ≠ 格式修复

- **症状：** 本仓库 #248 / 早期 #2：typeless Output、Feature 重建后格式未知、`unsupported-output-format`，NR=0。
- **上游：** [Zenless Zone Zero wiki](https://github.com/optiscaler/OptiScaler/wiki/Zenless-Zone-Zero)（2026-09 兼容表仍链到此页）：必须 DX12、`d3d12.dll`、`Dxgi=true`、FSR4+DLSS 输入时 Non-Linear sRGB 去黑屏、v0.9.3 quirk 修翻转 MV。[#752](https://github.com/optiscaler/OptiScaler/issues/752) 是「开 DLSS 被当成 XeSS / 黑屏」，不是 Output DXGI。
- **根合同：** 仍是 C1。NL-sRGB 是 **FSR4 输入色域旗标**，不是 NGX Output 视图。
- **建议原语：** 与 C1 相同；另加「Feature generation 使旧观察失效」。
- **可能帮助：** #248 只借 C1，不借 ZZZ quirk。
- **不抄：** 按 exe 的 ZZZ 特例；把 NL-sRGB 当成 R8 编码证明。

### C3 · 军团兼容表无格式信息

- **症状：** #202 要的是当前生命周期颜色视图 / 恢复，不是「看门狗 2」。
- **上游：** 兼容表一行 `Watch Dogs: Legion ✅ DLSS`，无独立 wiki、无 TYPELESS/曝光注记。
- **建议原语：** C1。#252 的 F6/掉帧不是这条。
- **可能帮助：** #202 Yes（同根）；#252 No。

### C4 · 曝光格式不认识 → 丢掉曝光（Opti / FSR）

- **症状：** 暗部压碎、洗白、XeSS 白屏（Jedi Survivor / SOTTR 例）。
- **上游：** [Known Issues · Exposure Texture](https://github.com/optiscaler/OptiScaler/wiki/Known-Issues)；`OptiScaler.ini` `InitFlags.AutoExposure`（SHA 锚 `21416401` Config.md / 现 master ini）。FSR2 文档要的曝光是 1×1 `R32_FLOAT`，不是 R8。
- **根合同：** 「格式不被中间层认识」≠「游戏没给曝光」。DLSS 指南 §3.9：1×1、首通道、多种格式、R16F 推荐。
- **建议原语：** 已确定类型则按该类型读首通道（我们已对 `R8_UNORM` 这样做）；TYPELESS/UINT/SNORM 继续拒绝。LDR 场景的 HDR 专用曝光纹理不应单独否决 LDR。
- **可能帮助：** #228 #229 只作为 **类确认 + 反面算法**；#68 仅当 F8 真出现 `exposure-texture-unverified`。
- **不抄：** AutoExposure 默认打开；扫描器白名单排除 R8。

### C5 · Dagherbou 扫描拒绝 `R8_UNORM` 1×1

- **症状：** KCD2「White point from = game exposure」一直 Reading；室内噪点。
- **上游：** [Dagherbou/OptiScaler_DLSSNR#41](https://github.com/Dagherbou/OptiScaler_DLSSNR/issues/41)（2026-09-07）。日志：`UAV dim 3 1x1x1 fmt 61 (filter rejected)`，随后 adopt `1x1 R16_FLOAT`。`fmt 61` = `DXGI_FORMAT_R8_UNORM`。
- **根合同：** 他们的过滤器比我们 **更窄**。#228 的资源正是 1×1 `R8_UNORM`、无 UAV。
- **建议原语：** 不要移植该过滤器。继续 typed R8 归一化红通道 + 真实 ShaderLoad 查询。
- **可能帮助：** 对 #228 是 **负对照**（证明「公开 NR 扫描帮不了 R8」）。

### C6 · HDR 编码身份 / 白点（RenoDX + Dagherbou）

- **症状：** NR 在跑但画面「没感觉 / 发绿 / 只有对比度 / 暗场噪」。Dawnwalker #177：ColorDiag `finalChanged=0`，曝光采样 1.0，不是格式旁路。
- **上游：**
  - RenoDX swapchain 预设 SDR / HDR10 / scRGB（`SWAP_CHAIN_OUTPUT_PRESET_*`）；技能文档强调「保留场景 HDR，不要对完成 SDR 做反 tonemap」。
  - RankFTW `rhi-repo` 发布 `renodx-dlss5-*` / `renodx-dlss-SF-*`（二进制 addon，**无**对应完整公开 NR 源；本仓库 `RENODX_2026_09_04_COMPARISON.md` 已划清这条边界）。
  - Dagherbou [#5](https://github.com/Dagherbou/OptiScaler_DLSSNR/issues/5)：v0.1.1.5 在 linear HDR（`IsHdr`）上 Colour>0 整帧发绿；v0.1.2 读曝光后颜色正确。v0.2.0 发布说明：highlight-starved proxy、多点曝光校准、读游戏曝光做白点。颜色合成归因 RenoDX MIT。
  - Opti Dawnwalker wiki：强制 tonemapper sharpening；与「弱」相关但不是格式。
- **根合同：** 模型残差必须落在 **与游戏相同的编码** 上；白点必须来自已验证曝光或显式 nits，不能猜。
- **建议原语：** `color.reason` 从 `unverified-hdr-*` 走到「编码已声明 + 残差按该编码回写」。可选：已读到的曝光纹理作为白点标量。默认关、可回退。
- **可能帮助：** #177 Yes。#253 只借用「编码必须证实」这句话，不借用 RenoDX 的 swapchain 升级。
- **不抄：** 强制 RenoDX addon；把 paper-white 扫场当默认；Opti `ForceHDR` / `UseHDR10`。

### C7 · 燕云 / 2K27 公开名单空

- Where Winds Meet：`✅ DLSS`，`dxgi.dll`，在线自担风险。无 output-color / R8 / TYPELESS。
- NBA 2K27：兼容表无条目。#68 早期记录还混有启用路径 / 未知 Feature；曝光只是可能阻断。
- **建议原语：** 无新上游原语。#253 继续本仓库有界 LDR；#68 先用 F8 看当前是否还停在曝光。

## 上游帮不上、必须自己做

| Issue | 为什么公开历史不够 |
| --- | --- |
| #252 | 记录症状是 F6 关仍掉帧 / 热路径，不是 DXGI 视图 |
| #247 | 旧候选对照；不能和 #253 合成一次失败 |
| #68（队列亲和） | 本审计只看曝光/TYPELESS；队列不在 Opti/Reno 格式史里 |
| #228 的 R8 读取算法 | 上游拒绝或丢掉该格式；算法已是我们的 |
| #253 的 unverified 门 | 无游戏级编码证据；Opti 直接写、不证明 |
| #248 的「重建后格式丢失」 | Opti 不维护「当前代次观察账本」；那是我们的 ledger/copy-scope |
| #177 的最终观感 | 上游给编码/白点思路；同画面 NVIDIA/游戏验收仍是我们的 |

## 给 #164 的可粘贴评论

GitHub Issues API 对本私有仓库是 403。下面整段可直接贴到 #164。

---

**Family A（格式/视图/曝光/颜色）— Opti/Reno 语料，2026-09-13**

完整行：`docs/interop/COMPAT_CORPUS_FORMAT_20260913.md`（本 PR）。没 vendor GPL，不建议原生 DLSS 强制走 Opti。

**先做**

1. **#2 / #248 / #202 合成一个解析器。** Opti `TranslateTypelessFormats`（`88c3f691c121d26de57333c80ea0ff01340a2ebd`）只证明 TYPELESS 是共性；**禁止**抄 `TYPELESS→UNORM/UINT` 表。下一步：当前 Feature 代的原生 SRV/UAV，重建后旧观察作废。
2. **#177 换合同维。** NR 已在跑。用 RenoDX/Dagherbou 的「编码身份 + transfer-matched residual + 已读到的曝光白点」，不要反 tonemap，不要挂中间件。
3. **#228 / #229 不要再向上游要 R8 读取。** Dagherbou #41 把 `fmt 61`（R8_UNORM 1×1）拒了；Opti 官方是 AutoExposure。我们 typed reader 留下，缺同候选实机。
4. **燕云 #253 / 旧对照 #247、军团掉帧 #252、2K27 队列 #68：公开史帮不上。**

**Top 3 Yes 的具体提示**

- **#2：** 观察器必须按「资源生命周期 + Feature generation」建键。Opti Hudless typeless 修的是他们自己的猜测 SRV；我们要对 **游戏已经创建的原生 view** 做同样的「不要看 GetDesc.Format」。
- **#248：** ZZZ wiki 的 NL-sRGB / 翻转 MV **对不上** `unsupported-output-format`。实机曾出现「同一块 `R8G8B8A8_TYPELESS`、旧代 UNORM 观察、新代未知」。下一步是代次失效，不是猜 UNORM。
- **#177：** Dagherbou #5：linear HDR 上套错色变换会整帧发绿、只剩亮度。Dawnwalker 要对的是「残差有、合成没动」。先固定编码身份再谈强度。

| 我们的 issue | 上游命中？ | 合同维 | 可能有帮助？ | 建议下一步 |
| --- | --- | --- | --- | --- |
| #2 TYPELESS generic view | 是 · Opti `TranslateTypelessFormats` + 88c3f691 | TYPELESS 存储 / 当前代次 typed view | Yes | 观察优先，禁止猜测表 |
| #248 绝区零 unsupported-output-format | 部分 · 同 TYPELESS；ZZZ wiki 无 Output 格式 | 后置 Output + 代次作废 | Yes | 与 #2 同一解析器；不抄 ZZZ quirk |
| #202 军团 format | 部分 · 表上只有 ✅ DLSS | 同 #2 | Yes | 原 exe；看当前稳态是否仍拒 |
| #252 军团 F6/掉帧 | 否 | 热路径 | No | 与格式分轨 |
| #253 燕云 output-color-unverified | 弱 · WWM 无颜色注记 | LDR R8 编码未证实 | Partial | 本仓库有界 LDR 证据 |
| #247 旧燕云颜色拒绝 | 否 | 历史对照 | No | 不与 #253 合并 |
| #228 DS2 R8_UNORM 曝光 | 反面 · Opti AutoExposure；Dagherbou #41 拒 fmt 61 | typed 首通道 | Partial | 保留 reader；要实机 |
| #229 DS2 前后置曝光 | 同 #228 | 同上 + 量化 | Partial | 与 DS1 分轨 |
| #68 2K27 曝光角 | 否 · 无 2K27 格式条 | 曝光只是可能门 | No | 先看 F8 是否真停在曝光 |
| #177 Dawnwalker HDR 弱 | 是 · RenoDX 合成；Dagherbou #5 / v0.2.0 白点 | 编码 / 白点 / 残差 | Yes | 验证编码后回写；不挂 Opti |

---

## 边界

- 公开 issue 搜索 API 对 `optiscaler/OptiScaler`、`clshortfuse/renodx` 本次返回空页；结论改走 wiki / 源文件 / 提交 / Dagherbou issues / RankFTW release。
- RenoDX DLSS5 addon 仍无完整公开 NR 源；只引用已发布说明、本仓库既有对照文档和 MIT 着色器/swapchain 合同。
- 本文件不含运行时代码、不含私有 F8/路径/哈希。
