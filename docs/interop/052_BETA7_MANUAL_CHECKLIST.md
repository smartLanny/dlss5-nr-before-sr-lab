# Beta 7：维护者游戏验收

先异环，2077 最后。核对交付清单中的 Beta 7 Core 和回退位置；由维护者启动游戏、选择原生 SR 或原生 RR。两种模式分开运行并保留报告，插件不会切换游戏模式。保持原有 INI；私有 SR 与外部累积互斥，实验功能沿用保存值，新安装默认关闭。

在固定场景打开“实验 → 开始自动截图对比”，关闭菜单，保持视角。九组依次为：原生无 NR、100% 普通 NR、50% 普通 NR、50% 私有 SR、25% 普通 NR、25% 私有 SR、50% 原生 MV、50% AMDOF、50% AMDOF 低频。按原有层数分别记录单层与多层；后续层范围另外设置并复验。

每组按配置、180 个有效源帧预热（上限 20 秒）、600 个有效 GPU 样本（上限 60 秒）、三张截图执行。不足、未采用或不支持会写明原因并继续；完成或“停止并恢复”后检查原设置恢复。原生无 NR 组没有插件 GPU 工作，其计时为不适用。后置原生 RR 的原生 MV 预期明确拒绝，不能算已经去噪。

本地截图清单 `screenshots.json` 和 `performance-gpu.csv` 记录原生路线、样本数、中位数、P95、完成 fence 和显存采样。GPU 时间只覆盖原生调用前后的部分 Core 区间，不能作为整游戏帧或总插件开销。创建、reset、切换、旁路与截图不计入有效计时；回调间隔也不是 GPU 帧时间。

画质分别比较同档普通恢复和私有 SR：暗发丝是否连续、细线和草叶是否粘连、文字是否变形、暗部有效结构是否消失、颗粒和闪烁是否减少。受控试验已经保留部分细线不如普通恢复的反证，不能仅凭锐化感判胜。

运动验收另录连续帧：静止、镜头平移、物体运动、遮挡显露、切镜、反射和 HUD。对照无累积、原生 MV、AMDOF、自适应和低频，并核对实际历史采用及拒绝原因。显露与反射在现有受控结果中没有普遍去噪收益。截图属于不同时刻，不用跨图 PSNR 代替同帧比较。

保留 Esc/切镜恢复、Present、地图省电、多层暗部验收。整游戏帧新增成本 ≤3% 需维护者用同场景、同 NR 工作量另测；超预算但有效的方案保持手动实验身份。两项省电功能不自动开启。出现崩溃时保留已有日志、截图和崩溃报告，再用独立回退包；不要删除失败证据。

# Beta 7: maintainer game acceptance

Test NTE first and Cyberpunk 2077 last. Verify the final Beta 7 identity and rollback package. The maintainer starts the game and selects native SR or RR; keep separate reports for each mode. Saved settings remain intact. Private SR and external temporal accumulation are mutually exclusive; new experiments default off.

Use **Experiments → Start screenshot comparison**, close the menu and hold a fixed view. The nine groups are native without NR, ordinary NR at 100%, ordinary/private NR at 50%, ordinary/private NR at 25%, native-motion accumulation, AMDOF and low-frequency AMDOF. Preserve layer counts and separately compare single/multiple layers and later-layer scope.

Each group configures, warms up for 180 valid source frames (20-second cap), measures 600 valid GPU samples (60-second cap), then captures three images. Unsupported, inactive and insufficient groups record a reason and continue. Completion or **Stop and restore** must restore original settings. Native without NR has no plugin GPU work to time. Native MV after native RR is intentionally rejected pending same-source guide proof.

The local screenshots.json and performance-gpu.csv report route, sample count, median/P95, completion fence and sampled memory. Timing covers only partial Core intervals around the native call, never the whole game frame. Creation, reset, bypass, switching and captures are excluded. Callback intervals are not GPU cost.

Compare private SR with ordinary recovery at the same workload, focusing on dark hair, continuous fine lines, grass, text, dark structure, noise and flicker. Controlled evidence retains some fine-line regressions. Separately record static, panning, moving-object, disocclusion, cut, reflection and HUD sequences with accumulation off and each supported temporal mode. Verify actual history adoption and rejection; current controlled results do not establish denoising in all revealed/reflection regions.

Retain Esc/cut recovery, Present, map power saving and multilayer dark-scene checks. The maintainer measures the ≤3% added whole-frame target separately at matching workloads. Effective over-budget options remain manual experiments. Preserve logs and failed evidence before rolling back.
