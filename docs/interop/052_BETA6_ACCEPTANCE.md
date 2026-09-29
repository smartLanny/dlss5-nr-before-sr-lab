# 0.5.2 Beta 6：软件验证与异环复测候选

编译源码 `e7407a39d12a6d43bf4e557f8c041e8361296831`；main 对齐 `b2990b120337cdd8026f83a1bd369f3708b78fbe`。完整统一构建 exit 0，用时 1276.1 秒。软件验证通过，6sol 已将中文 Core 放到异环并核对唯一插件、运行库和配置原字节，未启动游戏。**Beta5 一键对比的游戏失败仍保留；Beta6 游戏复测待维护者完成。**

## 本次故障修正

Beta5 在异环第4组“50% NR＋私有SR”首次 Feature1 Create 读取0xC。游戏同版 ReShade 的受控宿主复现了 NGX 代理对象域与 raw 命令列表混用；只转换描述符堆的中间方案通过 Create 但在 Evaluate 失败，已撤回。

最终私有 NGX Create/Evaluate 从当前 live AliasScope 借用原始调用接口，重验 epoch、raw 地址、token、Reset 和设备身份；代理 QI 解包必须等于当前 raw 列表。没有有效 alias 即拒绝，包括 raw 列表，不以 GetDevice 猜测初始化域。Core 自身采集、合成、提交及退休继续走原始 raw 录制，不重复初始化、不跨帧缓存代理。失败保留有效 NR 结果，F8 显示接口证明不足。

## 最终同源码证据

| 检查 | 结果和证明范围 |
|---|---|
| 完整统一构建 | 双语、CPU、WARP、既有累计正反例门通过；两种语言静态分析零诊断。继承的编译警告保留；D3D12 debug layer不可用，不称debug层通过。 |
| 原始调用接口窄回归 | 27项CPU/COM检查及真实接线结构检查通过，含缺alias、过期录制/epoch、错误raw/QI及引用平衡。 |
| 集中验收与截图 | 策略18项、完整生成Core控制器86项及错误恢复负对照通过；实际游戏前三组截图仅属Beta5历史，Beta6待重跑。 |
| 同Core WARP/显卡 | WARP17模式、显卡4模式通过，包含Flags75动态jitter/HDR、RR域和关闭态；人工参数与受控模型出口，不是NVIDIA推理。 |
| 原生接口真实NGX | SR＋2层Feature18、25%、641×385；RR-only＋5层Feature18、50%、1920×1080，均完成私有Create1/Evaluate5和提交退休，私有平面退休后0字节。 |
| 同版ReShade真实NGX | SR、RR-only各6帧，均为0层NR与人工B/Z，原始NGX出口，当前AliasScope；各完成Create1/Evaluate5/submit6/retire1，私有平面退休后0字节。缺alias反例无创建/采集/分配。 |
| 本地Core-only OTA | 中英文ZIP、组件哈希与十项许可通过；固定Manager `70af75c1e9398dd3e7363b7816942c9b3d4ed35a` 的实际解析器通过，未操作Manager UI。 |

**未通过的组合夹具保留：** 把整个Feature18链放到ReShade代理录制的尝试造成另一方向的接口混用，不能合并前两项结果宣称“ReShade＋多层NR＋私有SR”已验证。游戏全链稳定性仍需维护者复测。

最终各命令、夹具/生成源码哈希、运行库身份及计时/显存原始记录见[完整交付清单](052_BETA6_DELIVERY.json)与 [本地软件回执](%DLSS5_PRIVATE%/evidence/beta6-software-validation.json)。短检查的 `timing_samples=0`，median/P95的0是尚无有效稳定样本，不是零开销；峰值平面账目与DXGI总显存分别保留，退休平面0不代表驱动总显存回到0。

## 交付身份

| 语言 | Core SHA256 | 本地 OTA SHA256 |
|---|---|---|
| zh-CN | `fffcae611a6efef37f6c838255e3bc3df4c315f826b91425dc9bff156ab7736e` | `d2c301ebe6c373a00aba1b6740e717d9d3453661c6be436ec75f3e6a2d353eb4` |
| en | `e2529fa9cd7ce5cf3b607c4080b8baa4ca8fa55e4aedaf4528fa6b63eaee7971` | `e7c536f9ad9a4bda1f2e8fa77b915cafa913f2e77e8ce1ef4a4d59227c65c630` |

nrchain SHA256 `1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2`。20/30运行库 `6dac1b40f0c87af84a8177b18c741e84fb0c914f204c9d87d95916b665ba3af8`，40/50 Kio v1.0.6 `a2d16f9fba2b619559427821169918df56fc8e410a6ac87569953598a00b66fa`；Core OTA不包含运行库，不覆盖INI。

[安装回执](%DLSS5_PRIVATE%/evidence/052-beta6-nte-install-20260928/installation-receipt.json)确认原文件与配置；[回退包](%DLSS5_PRIVATE%/evidence/052-beta6-nte-install-20260928/rollback-portable.zip)恢复部署前Beta5原字节，Beta5仍有已知第4组崩溃，不称稳定版。旧Beta4和其他候选继续保留，也不冒充完整游戏验收。

## 下一步

按[简短手测](052_BETA6_MANUAL_CHECKLIST.md)在异环实验页运行一键截图，优先确认第4组首次私有创建、采用和截图，再看后续组与恢复。相对D21变化见[中文](052_BETA6_CHANGELOG.zh-CN.md)/[English](052_BETA6_CHANGELOG.en.md)，起点为 `cbfb33919696a2840ed5154c91abaea56228b661`。图片是不同显示时刻，不是同帧B/Z对照；截图与性能分段。

噪点/闪烁/拖影、Esc/地图恢复、质量与整帧新增开销≤3%仍待游戏验收；4K时域历史成本多峰，无稳定低开销承诺，无无收益的ResidualDenoise开关。独立RR/NVOF/模型光流继续保留独立研究身份。先异环、2077最后，由维护者测试。继续#313草稿，未合main、未在线发布OTA、未改变Manager默认版本。
