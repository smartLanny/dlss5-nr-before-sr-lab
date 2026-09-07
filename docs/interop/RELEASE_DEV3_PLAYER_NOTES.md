# 0.4.5 兼容增强测试版：更新了什么，怎么用

这是在 fix2 基础上继续修复、并合入兼容支线的新 OTA **预发布版**，内部版本 `beta0.4.5-multiapi-dev3`。适合已有本插件环境的玩家尝试；旧版继续保留，方便回退。

## 这次有什么改进

- **更容易设置画面。** 色彩强化的新装默认和“一键推荐”统一为1.0；色彩/HDR收进高级画质。以前把模型强度设成2的玩家，可以只恢复模型强度1.0，不用重置全部选项。模型强度、色彩强化和最终强度放大仍是不同参数。
- **扩大可尝试的游戏范围。** 合入 DX11 已有 DLSS 路线，改进颜色、运动信息和曝光纹理的读取兼容；D3D12 原生 DLSS 继续保留。并不是所有 DX11 游戏或没有 DLSS 的游戏都能使用。
- **增加光线重构后的 NR 路线。** 游戏原生光线重构（RR）成功后，可在满足深度等条件时尝试 NR；退出 RR 后仍保留你原先选择的前/后置模式。条件不满足时保留游戏原图。
- **后置也能调整 NR 处理精度。** 可尝试100%、75%、67%、50%，用来比较画质和开销。它调整 NR 的处理尺寸，不改变游戏内 DLSS 档位，也不保证一定提升帧率。
- **修复已确认的代码问题。** 继承真实 Reset 漏记修复，补齐 RR 资源释放，纠正桥接过早放行和超时释放资源的问题。NR 失败时保留原生输出，不把跳过处理显示成成功。
- **反馈更完整。** F8仍只生成一个TXT，补齐后置状态、真实命令状态和匹配的桥接日志，便于继续定位“有计数但没效果”等问题。
- **纠正RR状态名称。** 性能日志中的RR标记只表示观察到RR所有者；不再把它误写成“旁路”。有没有执行NR，仍看实际成功与提交记录。

## 更新后建议怎么操作

1. 退出游戏，把现用同类插件及配套 `nrchain_nvngx.dll` 备份到游戏目录外。
2. 中文、英文包二选一，更新包内核心、配套 DLL 和 dev3 carrier。只保留一套本插件，不同时加载新旧核心或两个语言。保留原有 INI、ReShade、NVIDIA 运行库和其他 MOD；本 OTA 不附带替换 INI。
3. 先保持游戏内 DLSS 档位不变，NR精度100%。进同一个原场景，按F6比较开关，再比较前置和后置。如果以前模型强度设为2，先点“仅恢复模型强度1.0”。
4. 基础效果稳定后，再尝试降低后置精度。遇到无效、闪烁或偏色，按F8把这一个TXT交回原Issue；画面问题方便时补F7，已有资料无需重复提交。

## 目前仍需注意

这不是“所有旧BUG都已修好”的版本。异环开场交替、SR之后第二次状态检查、部分游戏后置效果/HDR/肤色和长期稳定性仍待实测，相关Issue保持开放。未观察到真实 blend 四值时会明确报告缺失，不填默认值假装有证据。

DX11桥接为保证输出确实完成而增加等待，可能损失性能；超时后停止本次桥接并保留原生画面。若持续异常，退出并恢复备份，不需要反复试崩或更换驱动。

本包不包含无DLSS Feeder、原生 Vulkan、DX9/10/OpenGL兼容层或新的帧生成/MFG控制，也不含游戏文件、NVIDIA模型或首次安装环境。实际本地门禁见包内 `validation.json`；WARP测试不等于NVIDIA模型和游戏通过。具体游戏基础测试结果以Release所附实际记录为准。

## English

This prerelease combines fix2 with native-DLSS compatibility work: clearer picture controls, colour/model defaults, typed colour/motion/exposure access, eligible native D3D12 RR post-NR, and 50–100% post-NR work resolution. It fixes confirmed release/completion defects and keeps F8 as one TXT. Install one language with its matching components; preserve your INI and existing runtime. Start at 100%, compare F6 and pre/post in the same scene. DX11 synchronization may cost performance. Unresolved game issues remain open; no universal compatibility, Feeder/Vulkan/Legacy or new frame-generation support is claimed.
