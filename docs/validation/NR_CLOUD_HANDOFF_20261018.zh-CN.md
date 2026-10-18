# 新 NR 网络与连续 SDR 色控：云端交接

## 当前可审查成果

2026-10-18 重新核对 main `024f9cc32778c1056db364d76d087ee257da6502`，
以及 [#309 的 310.9.16 实施规格](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/309#issuecomment-6233027890)
和 [#274 最新补充](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/274#issuecomment-6233033039)。
本轮交付两组可独立审查的源码改动：

| 改动 | 源码与依赖 | 说明 |
| --- | --- | --- |
| 全局与五层独立网络选择、运行库身份、创建与安全拒绝 | `codex/309-per-layer-networks-20261018`，基于 #341 `f9ef953faad0a31d280d27adbbe411433a352a47` | 使用已有精确录制/队列/fence 退休；候选源码身份为 `0.5.2-beta.17` |
| 连续 SDR 通用微调 | [PR #345](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/345)，head `e0789b35ccf58011d117538b96d8de643b7c8837`，直接基于 main | 默认关闭；独立颜色切片，无单独发布版本；进入批准后的同一候选再打包 |

这两组是待 Windows 验收的源码。没有生成或重命名 Core 二进制，没有部署、升级
运行库或修改 Manager pin。当前本机未推送工作、已封存 Beta16 和 #340/#342/#343
的成果仍须由原整合者核对，不能仅凭这两组 PR 就宣称形成了完整 Beta16 后继组合。
Claude 继续负责 main 整合；网络 PR 只有 #341 这一层明确依赖。

## 菜单与配置如何使用

网络入口在现有“工作量”和“模型与层数”设置中。全局选项供选择“跟随全局”的层
使用；第一层到第五层均能独立覆盖。减少层数保留后续层设置，再启用时恢复原值。

| 配置值 | 含义 |
| --- | --- |
| `NRNetworkPreset=0` | 全局跟随当前运行库默认 |
| `NRNetworkPreset=1` | 全局请求 Original/HT |
| `NRNetworkPreset=2` | 全局请求 Optimized/Small FP8 |
| `Layer1NetworkPreset=-1` 至 `Layer5NetworkPreset=-1` | 该层跟随全局 |
| 任意层显式 `0` | 该层自行跟随运行库默认，不继承全局 |
| 任意层显式 `1` 或 `2` | 该层请求 Original 或 Optimized |

缺少新键的旧配置使用全局 0、所有层 -1。新 DLL 的默认网络可能不同；需要旧网络
对照时显式选择 Original。网络与 Style/强度/最终色彩是独立参数，不用旧 `NRPreset`。
仅改变继承写法而有效请求不变时，不做无意义的模型重建。切换 DLL 仍须退出游戏后
重启；同一 DLL 内切换网络沿已有安全退休后重建机制。

五层独立请求已经接通。混合 Original/Small 的配置需要额外验证权重驻留峰和画质，
首轮内部验收先用所有活动层同一网络。不同请求同时驻留时，不能套用官方的单模型
显存降幅来预测插件总显存。

颜色入口沿原“光影／通用微调”。`GenericResidualAlgorithm=0` 保留 Legacy HSL；
`1` 选择 Continuous SDR。五个增益保留；总强度仍为 1–2，其余为 0–2。
新模式只在已有明确 linear/sRGB SDR 709 合同的最终消费者启用。菜单会说明
unknown/HDR、raw、低网格等未采用原因，不能靠猜输入编码来解锁。

## 本轮确实验证到哪里

- 网络：严格配置解析/保存、五层有效请求和历史身份、实际 Create 参数绑定、
  实际 caller 拒绝传播、精确录制退休接口、已有 TXT/JSON/F8 与完整重放参数记录，
  均有生产函数提取的 CPU 检查及故意破坏实现的负控。
- 运行库：正常帧不读 DLL 文件；现有后台 worker 按加载代次读取已加载模块路径的
  文件身份。离线检查器对一个显式文件做有界 SHA256/PE 检查；合成 PE 与异常输入
  测试通过。真实候选 DLL、Authenticode、实际加载/NGX/显卡未在本机检查。
- 颜色：新数学 64,120 断言、实际新 UI/记录分支 66 项检查及六组负控通过；
  终端亮度与 LightingLock/Detail 邻域使用同一标量控制。既有颜色/重建/光照等
  定向数学回归结果见 PR #345 的验证文档。
- 着色器：最终颜色源码中的实际嵌入 HLSL，官方 Linux DXC 1.9.2609、`cs_6_0`
  编译 29 变体通过。该证据不替代 Windows D3DCompile/`cs_5_0`，DXIL 不可放入
  产品的 SM5 cache。
- 完整便携 suite 曾运行一轮，runner 摘要为 49 PASS / 8 FAIL / 3 SKIP；其中
  42 个文件确实执行检查并成功，另外 7 个成功退出的文件内部全跳过，总内部 skip
  为 38。5 个 GCC 编译失败已在干净 #341 上精确复现；3 个失败依赖浅快照缺少的
  历史。它发生在最后的创建拒绝修复之前，最终定向回归另记；不能称整套通过。

详细合同、失败边界和可重跑命令见
[网络验证说明](NR_NETWORK_SELECTION_20261018.md) 与
[颜色验证说明](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/e0789b35ccf58011d117538b96d8de643b7c8837/docs/validation/2026-10-18-generic-continuous-sdr.md)。
源码改动没有增加全屏 pass、NR 调用或为色控新建纹理；实际 GPU 指令成本、
全链时间、P95/P99、创建/切换峰值显存和净画质仍无本轮实机数据。

## Windows 恢复后的最短接续顺序

### 1. 冻结源码与候选文件

由整合者核对本机未推送成果，先收口 #341，再把网络与需要的颜色切片纳入一个
明确源码身份的候选。保留旧包和旧运行库；新旧 DLL 分别放在独立候选目录。
不为本轮另建 renderer、扫描参数组合或恢复已失败的私有 SR 候选。

```powershell
python scripts/inspect-dlsnr-candidate.py $OldNr --output $OldReceipt
python scripts/inspect-dlsnr-candidate.py $NewNr --output $NewReceipt
Get-AuthenticodeSignature -LiteralPath $NewNr |
    Format-List Status, StatusMessage, SignerCertificate
```

变量须指向合法取得的候选文件和新的输出文件。CLI 成功只说明检查完成，不表示
模型已执行或支持已验证。当前新文件收据为
`2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f`；
不匹配时按独立候选记录，不能靠相同版本字符串冒充已核对文件。

### 2. 沿已有完整构建入口

使用 `scripts/build-unified-candidate.ps1`，提供已有且核对过的
`UpstreamRoot`、`ToolchainRoot`、`ReShadeSourceRoot`、`DirectXMathRoot`、
`FaceRuntimeRoot`、`FfxSdkRoot`、`FfxCacheRoot` 和新的 `OutputRoot`。
Carrier 固定于 `28aed4099b0fe1c207b20b5fee5364c0773c25c2`；其余精确 pins、
完整 face backend 和全部 canonical gates 由现有构建记录约束。
本轮不新增自动收费 CI，也不因 Windows 暂缺而跳过保护门生成发行包。

### 3. 复用现有宿主做四个隔离进程对照

使用 `scripts/validate-compat-present-ngx.py` 的已有 frozen Core、compiler、
ReShade、SDK/vcvars 输入；新增的参数只有网络请求。每臂使用独立输出目录与进程，
保持相同源码、输入/导引、曝光、R/N/O、普通恢复、预热/观察窗口，首轮单层 100%、
FG 与外部时域/私有 SR 关闭。

| 臂 | 运行库 | 新 runner 参数 | 目的 |
| --- | --- | --- | --- |
| A | 固定旧文件 | `--case single --network 1` | 旧基线 |
| B | 固定新文件 | `--case single --network 1` | 分离 DLL 升级与网络变化 |
| C | 与 B 相同 | `--case single --network 2` | 检查新网络 |
| D | 与 B 相同 | `--case single --network 0` | 实测默认行为，不靠版本推定 |

宿主读取创建参数只能证明请求被写入，不能证明 runtime 内部执行了哪个网络。
输出比较保留 P→原始 NR A→最终 Z，并参考同模型重复误差；F8 中
`networkExecutionVerified=false` 不应被测试脚本强行改成 true。

单层通过后再走三层全 Small、1→2→1 切换、off/on、resize 及必要故障门；五层
和混合网络是后续有界兼容样本。`--networks 2,2,1` 已能驱动一个固定三层场景，
但不作为首轮推荐参数，必须额外记录创建和切换的权重驻留峰。

### 4. 最后再给维护者一段筛选后的游戏短测

内部通过后，在一款已有 SR/RR 场景中比较同 DLL 的 Original/Small：静止→平移→
停镜，先固定 N，再按实测模型预算提高 N。脸部、头发、细线、暗部、高光与强彩光
都要同时看原始 NR 和最终输出；不自动把强度、色彩或 Transfer 乘 2。

颜色对照固定网络及其它参数，只比较 Legacy/Continuous 同增益；另核对一次
已持有的 HDR 素材仍走原合同。最终只交一个有完整 source/package/binary hashes
的候选；没有实机净收益证据前，不把少编辑、旁路或单模型成本下降称为游戏优化。
