# beta0.4 Perfect Chain r2 实机验证表

分支：`codex/beta0.4-perfect-chain`

## 安装

只使用构建包中的一种语言版 `.addon64`，并把 `nrchain_nvngx.dll`、`nr_before_sr.ini` 与你已有的签名 `nvngx_dlssnr.dll` 放在同一个 ReShade Add-ons 目录。不要同时加载中英文两个插件，也不要同时开启 ShortFuse/RenoDX 的 Direct Neural Rendering。

## 第一轮：先确认链路正确

1. 游戏内关闭 Ray Reconstruction，只保留普通 DLSS Super Resolution。
2. `Mode=2`，工作分辨率选 `Reference`，效果强度 `1.00`，最大亮度倍率 `4.00`，颜色跟随 `0`。
3. 观察面板必须显示 `Feature 18 success` 持续增加，且 `transfer recorded / bound / submitted` 三项都增加。
4. 用 F6 比较插件开关；关闭时应是真正的纯游戏 SR，不执行 Feature 18。
5. 按主键盘 `-` 截显示画面，再点一次 `Capture NR stages once` 采集原图、代理、NR 输出和合成图。

## 第二轮：性能档位

固定同一镜头、同一 DLSS 档位，依次测试：

- Reference：质量基准。
- Quality：推荐默认。
- Balanced：优先测试的性能档。
- Performance：观察细纹理、文字、发丝、远景闪烁。
- Ultra Performance：只作为极限性能/稳定性实验。

记录面板中的 `NR 工作分辨率 -> render -> SR output`、平均帧率、1% Low 与 GPU 占用。若面板无法证明 SR 输出尺寸，r2 会自动回到完整渲染分辨率，不会偷偷按渲染分辨率再降到 58%。

## 第三轮：乘算强度

在 Reference 或 Quality 下依次测试 `1 / 2 / 4 / 8`：

- 1：模型原始比例。
- 2：等价于把模型的明暗倍率乘算两次。
- 4：强效果。
- 8：极限诊断，不建议作为默认。

先保持最大亮度倍率 `4`，这样 8× 强度仍被硬限制；只有为了观察极端上限时再逐步提高到 `8` 或 `16`。颜色跟随先保持 `0`，确认亮度/结构稳定后再试 `0.5 / 1 / 2`。

## 必测压力场景

- HDR 关闭/开启与快速室内外曝光变化。
- DLSS Quality/Balanced/Performance 切换。
- DRS 波动与分辨率重建。
- RR 关闭 -> 开启 -> 关闭；若游戏保留 RR handle，按提示重启后再测。
- 镜头切换、读档、过场、快速转身。
- 霓虹灯、太阳、火焰等高亮运动物体。
- 人脸、头发、栅栏、树叶、电线、HUD/字幕。

## 反馈材料

每个异常至少保留：

- `nr-before-sr.log`
- 显示画面截图
- 对应一次 stage capture 文件夹及 `metadata.txt`
- 游戏名、分辨率、HDR、DLSS 档位、显卡/驱动
- WorkMode、Strength、MaxLumaRatio、LightingLock、EdgeGuard、ProxyCurve、ColorStrength

若发生崩溃，请同时说明是在启动、Feature 18 创建、切档、开关 RR、读档还是退出阶段。
