# Beta 5：一键画面对比与手动验收

进入实际游戏场景，在“实验”页点击“开始自动截图对比”。固定视角并保持游戏运行，等待记录完成；需要中止时重新打开菜单，点击“停止并恢复”。完成后点击“打开对比页”。不必逐项改比例、切开关或按截图键。

临时设置只在本次会话使用，完成、取消、运行时退出或 Present 停止后恢复原插件设置，不写入用户 INI。保留原模型、层数、强度和色彩参数。游戏自己的 SR/RR、HDR、FG 和相机由用户控制；这些截图来自不同显示时刻，并非同一帧的像素差对照。

| 顺序 | 自动采集内容 |
|---|---|
| 1 | 关闭 NR，保留游戏自身渲染与原生 SR/RR |
| 2 | 后置 NR 100%，空间恢复 |
| 3 | 后置 NR 50%，空间恢复 |
| 4 | 后置 NR 50%＋私有 SR |
| 5 | 后置 NR 25%，空间恢复 |
| 6 | 后置 NR 25%＋私有 SR |
| 7 | 后置 NR 50%＋游戏 MV 累积 |
| 8 | 后置 NR 50%＋AMDOF 累积 |
| 9 | 后置 NR 50%＋AMDOF 低频时域 |

每组预热后最多三张。无法准入、未实际采用、画面尺寸或配置变化、截图失败均记录原因；缺图不能当作效果一致。无需强行补齐不支持的路线。只有原生后置准入成功才采集 NR 对照，私有 SR 不因此扩展到 Present。

先看固定视角的细节、暗部、文字和增强量，再单独观察走动、遮挡、切镜与菜单恢复。三张静止截图不能证明没有拖影，也不能代替运动验收。HDR 截图沿用 ReShade 的保存格式，浏览器显示不代表线性 HDR 原始像素。

截图与性能计时分开。截图过程包含切换、预热和磁盘写入，不用这一段帧数评估开销；需要性能对照时使用原有“集中验收”，保持同模型、同层数、同场景。Beta 4 的受控成本尚未证明稳定低开销，Beta 5 不宣称降噪收益或整帧开销 ≤3%。

OTA ZIP 只包含一个 Core 和配套 nrchain，以及清单、许可与更新说明；通过现有 Manager 导入并手动选择。退出游戏后安装，保留 INI 与原运行库。旧 D21 的辅助层比例迁移到统一工作尺寸，升级后先核对工作量。NR 运行库更新是独立操作，不能因为只更新 Core 就显示模型库已升级。

全部游戏由维护者操作：先异环，2077 最后。6sol 仅部署、核对文件和准备回退。软件回执、二进制与 OTA 哈希以 Beta 5 的交付清单为准；旧 Beta 4 包和回退证据保留。

# Beta 5: one-click comparison

In a live game scene, open **Experiments → Start screenshot comparison**, hold a fixed view and leave the game running. The controller temporarily changes plugin settings, warms each group and requests up to three pictures. Use **Stop and restore** to cancel, or **Open comparison page** when finished.

The nine groups are: NR off; spatial NR at 100%, 50% and 25%; private SR at 50% and 25%; native-motion accumulation, AMDOF accumulation and AMDOF low-frequency temporal at 50%. Model, layer count, strength and colour choices remain intact. Unsupported or inactive groups are explicitly skipped. Settings are restored without writing the user INI.

Game SR/RR, HDR, FG and camera are not switched automatically. Images are saved at different display times; they are not matched same-frame model pixels. ReShade's encoded HDR screenshots do not establish linear HDR equivalence. Use motion separately to check trails, occlusion, camera cuts and menu recovery. Screenshot runs are not performance measurements.

The optional Manager-importable OTA contains Core and nrchain only, with checksums, notices and a changelog. Exit the game before installation and retain existing settings and runtime libraries. D21 auxiliary scales migrate to the primary work grid, so verify the effective workload after upgrading. Runtime updates remain separate. Game quality, stability and a ≤3% whole-frame cost remain unverified.
