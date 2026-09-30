# Corefix.7：运动矢量轮换时选择安全槽位

## 冻结来源与产物

运行源冻结为 `7d68fef12958764b05429eec98f18e9cb134a73b`，接续 Corefix.6
交接提交 `b97a1ac9fdc2f69c0c6a0fab2c65e20a7ece7766`。版本为
`0.4.7beta-corefix.7`，实际 Core PE 为 `0.4.7.12`。

首轮同源完整验收 `evidence/corefix6-retest-20260911/build-r1/validation.json`
通过 80 项检查，包含 CPU、双语编译、实际 WARP。源码清单 SHA-256 为
`071c4615c082a5322d32ae0845e9b670c11d2d3d2802d46dd056f362bd9f90cc`，
验收结果 SHA-256 为
`b2310d67bd04c5ffb9b051969ed349cf1d9b2bdcf6c6bb1b2c6f1d632c09bd87`。

| 产物 | SHA-256 |
| --- | --- |
| 中文 Core | `d3e3420a457b5bca740b066be1156e6f269c199eaa21f8fe561830966ea7fcc3` |
| English Core | `633b7e2199f47634df09941f3f9bb05251550b51978e4d8084f09876616e025d` |
| `nrchain_nvngx.dll` | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

## 缺陷与修复

Corefix.6 的绝区零前置、后置复测仍强烈闪烁。当前窗口有 27 条 Motion
描述符修改被延迟、保留原生 SR 的明确记录；同 scope 的 44 次 carrier Evaluate
仅增加 14 次 NR 成功和 14 次 transfer 提交。这些计数不能当作最终显示帧率。
格式与队列证据的详细边界见 [Corefix.6 复测分析](COREFIX6_RETEST_MOTION_20260911.md)。

旧 `FindOrCreateSlot` 只按 Color/Output 和 exposure 返回首个槽位。
当输出不变、Motion 纹理轮换时，可能选中绑定旧 Motion 的忙碌槽位，导致
`EnsureMotionProxy` 正确拒绝修改，即使另有空槽也会跳过 NR。

新选择器在同一 `NrContext` 内先寻找完整的当前 Motion 绑定，再寻找可安全
修改的同资源槽位；没有合适候选时继续使用已有空槽分配和安全回收路径。
PRE 与最终组装的 POST 都传入本次 resample 所需 Motion。非 resample 路径
保留原来的外部资源匹配方式，生成器检查 POST 调用没有遗漏这个参数。

8 槽上限、单一 NR handle 与 temporal history、GPU 完成 fence、录制引用、
待发布 fence、队列及格式检查保持不变。既有绑定可复用不等于整次 NR 调用
已获准，后续队列和录制检查仍独立执行。无安全槽时仍保留原生 SR。

## 验证

- CPU 直接编译生产选择器 header，五组场景覆盖后方精确绑定优先、忙碌但已完整
  绑定、缺少 proxy、交回空槽路径和非 resample 复用；恢复旧首命中策略后，
  `OLD_STRATEGY_ORACLE` 等断言按预期失败。
- 完整组装 Core 的 native-color WARP 夹具通过 11,293 项检查。新增 PRE/POST
  两组用真实 command list 和未完成 fence 保持 A 槽忙碌，直接改绑继续拒绝，
  B 使用另一槽；完成后由生产 Motion shader 读回 A/B 首像素，核验原描述符
  内容。一个 context 的 handle 保持，八槽忙碌时第九个绑定被拒绝。
- 此次 native-color 运行的 D3D12 debug layer/info queue 均不可用；WARP 执行、
  fence 和读回断言实际通过，不把它记录成调试层验收。
- 既有资源恢复、格式、原生状态、精确提交与回收、存储量化、曝光和合成检查
  一并通过。测试使用构造输入或脚本化 NR 回调，没有加载 NVIDIA NR 模型。
- 冻结源码审计核对 74 项实际 blob pin；新增类别仅为自有 header、测试和说明，
  没有引入外部源码、运行库、模型、原始反馈或凭证。已有私有证据保持不变。

## 交接边界

本轮没有运行新的菜单验收，没有更换游戏文件。真实 NVIDIA NR、游戏闪烁是否
消失、最终显示及性能仍待验；不能把槽位回归通过等同于游戏视觉通过。

只安排当前绝区零 POST 原场景约 30 秒、一份 F8，并反馈 NR 是否持续可见、
闪烁是否持续及格式提示是否返回。保留 HoYoShade、INI、ReShade、运行库和画质，
优先只更换 Core。[中文复测指南](../../testing/CORE_047BETA_COREFIX7_ACCEPTANCE.zh-CN.md)／
[English guide](../../testing/CORE_047BETA_COREFIX7_ACCEPTANCE.en.md)。

死亡搁浅既有无持续效果和偶闪保持开放，本轮不要求其重复四阶段复测。
#2/#205/#203 不据本地通过关闭；不自动合并 main/release、改 tag 或发布稳定版。
