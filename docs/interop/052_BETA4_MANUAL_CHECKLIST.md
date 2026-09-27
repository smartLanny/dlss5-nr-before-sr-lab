# Beta 4 手动检查 / Manual checks

异环已部署 **0.5.2 Beta 4 中文 Core＋Kio 40/50 NR**，保留原 INI，未启动游戏。[完整身份](D:/ChatGPT/DLSS5/deliveries/052-beta4-acceptance-20260927/DELIVERY.json)。异环已由 6sol 部署中文 Core 与 Kio 40/50 NR 库；[安装回执](D:/ChatGPT/DLSS5/evidence/052-beta4-nte-install-20260927/installation-receipt.json)确认唯一 Core、配置原字节和原生 SR/RR 不变，未启动游戏。[回退包](D:/ChatGPT/DLSS5/evidence/052-beta4-nte-install-20260927/rollback.zip)及[使用说明](D:/ChatGPT/DLSS5/evidence/052-beta4-nte-install-20260927/README-rollback.md)位于搜索目录外，回退脚本 VerifyOnly 已通过。

## 中文

1. 核对候选 JSON 中的版本/哈希、单一 Core 和插件目录外回退；备份 INI。先异环，2077 最后；先关 FG，按需手动设游戏 RR/HDR。
2. 确认私有 SR、外部累积、两项省电对新安装默认关闭；保留已有保存设置，包括旧实验 jitter，**不要为了本次验收重置 INI**。没有独立 ResidualDenoise 空开关。
3. 先确认普通 NR 正常，再依次运行集中验收五方案：当前设置、比例/色彩、时域路线、私有 SR、普通/低频。每组先预热，在 F8 核对当前配置/实例、实际 GPU 采用及拒绝原因；旁路/未采用不算通过。
4. 私有 SR 先独立比较低比例 NR＋私有 SR与较高比例普通后置 NR，记录质量和成本；成本开关对照必须固定同一 NR 工作量。固定视角，检查暗部、文字/细线、运动物体、显露、切镜、反射/HUD和低比例多层；手动留对照图。图片采集结束后另开性能段，记录绝对 ms、median/P95 和内存。受控 4K 成本有多峰，游戏整帧 ≤3% 尚未证实；不得把局部 GPU 时间当整帧分母。
5. 分别正常完成与中途取消，确认插件设置恢复且游戏设置未被改；关功能后检查对应工作停止、资源按真实完成/录制结束回收。收集 F8 与异常截图，再按已核验路径回退。

## English

1. Verify the candidate JSON version/hash, one Core loading location, rollback outside plugin search paths, and back up the INI. Test 异环 first and Cyberpunk 2077 last. Start with FG off; set game RR/HDR manually.
2. Confirm private SR, external accumulation and both power-saving options default off for a fresh install. Preserve saved choices, including legacy experimental jitter; do not reset the INI for this acceptance. There is no empty standalone ResidualDenoise switch.
3. Confirm ordinary NR first, then run the five acceptance programs: current settings, scale/colour, temporal routes, private SR, and ordinary/low-frequency. Warm up and inspect F8 for the current configuration/instance, actual GPU adoption and refusal reasons. Bypass is not a pass.
4. Separately compare lower-scale NR plus private SR against higher-scale ordinary post-NR for quality and cost. Keep the NR workload fixed for incremental-cost on/off comparisons. Inspect dark regions, text/fine lines, moving objects, disocclusion, cuts, reflections/HUD and low-resolution multilayer cases. Capture comparison images manually; measure performance in a separate interval. Record absolute ms, median/P95 and memory. Controlled 4K cost has multiple peaks; the full-game-frame ≤3% target remains unverified.
5. Test both normal completion and cancellation: plugin settings must restore without changing game settings. Turning a feature off must stop its work; retirement requires real completion and ended recordings. Save F8/screenshots and use the verified rollback path if needed.

Deployment: Beta 4 Chinese Core and the Kio RTX40/50 NR runtime are installed in Neverness to Everness; the original INI is preserved. No game was launched. [Rollback instructions](D:/ChatGPT/DLSS5/evidence/052-beta4-nte-install-20260927/README-rollback.md) describe the verified original backup location. The script preserves later INI changes unless `-RestoreIni` is explicitly selected.
