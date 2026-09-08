# 2026-09-08 A/B/C/D 截止交接

维护者要求本轮及 Manager session 在北京时间 09:00 前停止；未完成内容进入
现有 Issues，不能为赶截止扩大架构或把样本测试写成游戏支持。
本页覆盖本批新增结果；下方旧交接中的“尚未链接/尚无 NVIDIA 样本”等描述
只保留其历史意义。生产源码基线为 `41696ccb64408397951577481051cbcf6944a446`；
08:31 再次 fetch 的 main 为 `cd10d96b817d83ea4d84da4286c52cbdc0ca377a`，
相对前者仅新增反馈归档，生产源码未变，本轮未将新原始反馈复制到实验分支。

## 已交付与验收范围

| 工作 | PR / 源码 | 实际验证 | 尚未完成 |
| --- | --- | --- | --- |
| A：Feeder 与自有 Core | [#171](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/171)，`3758ee58060145ed9d987313a9fd7996288a6aa8` | MSVC 链接；生产 Signal/Drain 故障检查；真实 NVIDIA Core 在 N=100%/50% 各成功 4 帧，含真实 fence 和读回 | ReShade 注册使用测试替身；完整游戏 Feeder/Present 与跨 API 验收未完成 |
| B：公开 FFX 描述直接映射 DLSS SR | [#174](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/174)，实现 `5ca02fdf6e5448e7886ae12979a920e65b9dd350` | 16 类真实 WARP 资源边界；RTX 5090 Laptop 上 SR-only 640×360→1280×720，3 次真实 NGX Evaluate、GPU 完成及全量读回 | 不是 AMD FFX DLL 游戏拦截；没有通用 TAA/TSR 提取或游戏部署 |
| A+B：自有 NR→直接 SR | [#175](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/175)，`b59c818c742d53f0735512874182bee6b003c706` | NR 关：8 次 SR；NR 开：8 次 SR，其中 5 帧真实 Feature 18→SR，其余为训练/预热；均有真实完成与有效输出 | 产品启动 hooks-ready 协议仍缺失；宿主等待已观察的就绪信号后才创建 SR。非游戏验收 |
| C：Manager 启动前 SR/FG 设置 | [公共 Manager #23](https://github.com/smartLanny/dlss5-manager/pull/23)，修复 HEAD `0b386eb2de1cc392cf69a8b6fb809800f9e1cc62`，按维护者最新决定冻结 | 已实现实际设置事务、NVAPI/指定 RTX40 v11 配置写入及恢复；185 项源测试、合成 Electron 流程和专用合成 EXE profile 实测 | **这是旧测试 UI 候选，不推荐合入或当作现用软件升级。** 现用 xiaofeng 产品仅接续版本/桥接 UI 小切片；SR/FG 有效逻辑的后续选择性迁移需独立记录 |
| D：0.5 追平与保留研究 | [#170](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/170)，`cd0fce687b06abaebec62db53694f7bca2dfa792` | 46 项本地门禁、双语 Core/nrchain/carrier 构建、生产 HLSL/WARP；504 个中性场景与 hotfix 生产 shader 输出逐字节相同 | 无新 0.5 游戏画质/性能验收；新核、EditField 与真实多次 NR 仍未完成 |
| D：前后置色彩合同 | [#172](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/172)，`6ee5494110677044f6c11fd697e6608e6e852326` | 72 组生产前/后置成对 WARP 对照，保留原始 N 平面与独立数值校验，40 组确有不同输出 | 是测量合同，不是新增画质算法，也不是游戏 F7 采集或多次 NR 性能证明 |
| FG：官方真实 renderer 样本 | [#173](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/173)，`a0143f29efa89ea384a6a435a384c3f204530e52` | 官方 Streamline 样本与本地固定 2.12 插件；121 个动画源帧，焦点/Reflex/设备状态正常 | Presented 最大为 1，输入 fence/VRAM 为 0；**没有证明生成任何中间帧**，#167 继续开放 |

本轮没有引入 OptiScaler 默认依赖。已有原生 DLSS 游戏继续使用现有路线；
FSR/XeSS-only 才需要新的时域 provider。TAA/TSR 提取是另一项实质工作，
不能用安装 DLL、计数成功或 FFX 描述样本替代。

## 实际构建锁与后续文档提交

PR HEAD 不自动等于二进制来源。文档/测试补正之后没有把旧二进制重新标为新构建。

| 对象 | 实际构建来源 / SHA256 |
| --- | --- |
| A Core | 源 `53fb7fb5bfee49bf37bfc7d50a45f62a54182088`；DLL `081c61f293710face535f0cdcf9011cdc257e6d319595dfb99ae2fb064627a18` |
| A Feeder | 源 `aa0ac0daa2ef0bc770e75cd0a8c3392c63d46f81`；DLL `52b70cc7ad4512740053438fdbed491b496bc23c81f7f7fefe2aeedfd20c850f` |
| B 生产 mapper / 测试源码 | `230614cf224e6161905ea16e006bec023ebcac1cc51bc425aad5ce89c572bc95` / `4b7f6880f8424d2b1f064fe1910a06fee49352bc3bbe5a5cb0bb32989f244486` |
| B 最终硬件 EXE | `fa9f4986dad64bb44fe756368cb53122121aaaac013b445d103b88e900267942`；运行时为 `c6389f7` 基线上的未提交新增文件，以上字节随后提交到 `5ca02fd` |
| A+B 联合宿主 | 源 `46c3b5e883bd196190ce2c6b750bf3aaeebf75a5`，Core 为上列 `53fb7fb`；EXE `02c4696fc1f3d909e19af71aebf35acd55275fba6372f7e84793ecf8ffb19627` |
| D 0.5-dev4 | 源 `bf0ef31d5e822586781a349bfda77bed527a99b6`，PE 0.5.0.4；7 文件 ZIP、688561 bytes、`561f77ee69a1b1baad273b1e3043a9a119da81c5bdf3e855f1997ad179955442` |
| FG renderer | 官方样本 `dd6e1803b97e5b3f218f5776d85f09e4c386c34f`；EXE `4ac11ef25cef621c71406dea811e5e6dd7bf533935b9c2e7861ade29f780b32`；依赖签名与哈希见 #173 文档 |

B 最终硬件与较早 WARP 运行分开记录；最终脚本补充 10 项依赖 pin 和未来
manifest 元数据后仅做静态检查，没有冒充又一次 GPU 运行。各轮早期失败、
初始化竞态与 FG 默认 OTA/失焦轮都保留，未计为通过。原始证据与供应商
SDK/二进制留在私有工作区，不进入源码导出。

Lab 托管 CI 因付款/额度未启动，不计通过也不重跑；公共 Manager 的 CI
独立运行，须以其最终 HEAD 的实际 checks 为准。本地 compile、WARP、
NVIDIA 推理、官方 renderer、真实游戏验收是不同证据层次。

## 合并关系

1. 维护线继续现有候选：#149（有限 blend 与 reason，#154 已去重关闭）→#150；
   #151/#156/#155/#157/#160 保留各自增量，之后 #166。不得重新实现或双合 #154。
2. #171 与 #174 都以 #166 为基底；#175 以 #171 为 base，**已普通合并
   #174 实现提交 `5ca02fd`**。若采用 #171→#175，#174 的实现已包含；其后
   纯文档收口另行保留，不能重复复制 mapper。
3. #170 目标是 `next/0.5`，已包含旧 #119 的完整祖先和 hotfix；#172 接 #170。
   旧 #73/#115/#119 研究保留，不再重放同样补丁，也不混进 0.4.6 稳定包。
4. #173 是以 #166 为基底的独立探针，不是可安装 FG 产品。
5. 公共 Manager #23 已包含 #16/#21 的有效增量，但维护者在 Manager session
   最新明确该组为旧测试 UI，**冻结归档，不在本批合入 #23**。这一决定替代
   早先“聚合一次合入”的建议；#21 的只读编译器可合判断另以 Manager owner
   最终审查为准。现用 xiaofeng 的后续迁移不能混用两套产品的版本、安装目录
   或状态格式，也不因冻结测试 UI 丢弃已实现事务代码。

上述实验 PR 提供可审查源码，不表示所有产品功能可发布。未擅自合并 main/
next、覆盖已发布 tag 或发布实验包。既有 0.4.6 与独立 hotfix.1 草稿保持各自身份。

## 下轮从哪里继续

- [#164](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/164)：先补可观察、可等待、失败有界的 Core hooks-ready 协议，覆盖早期 SR Create；保留 unknown-handle 保护。再做真实 Feeder/游戏输入与 API 传输，不另造 Core。
- [#14](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/14)：固定一个确实缺少 DLSS 的游戏/API/SDK，接真实 dispatch；依次 SR-only、NR→SR，核对原生回退、动态尺寸、曝光/运动和完整游戏生命周期。
- [#167](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/167)：保持同一官方 renderer，以 SDK 开发诊断解释 backend 的插帧条件及 `WAR4639162`；先证明生成像素/显示节奏，再接本项目 NR/SR。不要把 121 个源帧当插帧成功。
- [#168](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/168) / [#169](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/169)：以追平后的 0.5 和已实现的原始 N 测量合同接续算法与成本实验；游戏画质、EditField/新核、多次 NR 尚未验收。
- [#162](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/162)：**人脸遮罩反复开关后视觉保护失效是维护者已报告的当前 BUG，等待维护修复与后续专门测试。** 参数重建/继续提交不能否定此问题，也不能冒充独立最终脸部倍率已修。
- Manager #18/#19 及现用产品迁移、Vulkan layer/shared refs 由指定 Manager session 继续记录；本轮不能完成的部分以其最终 issue 清单为准。

细项证据：[#171/#175 文档](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/codex/core-sr-combined-20260908/docs/interop/COMBINED_NR_SR_HOST_20260908.md)、
[B 直接 SR](DIRECT_SR_20260908.md)、[#173 FG 样本](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/codex/fg-sample-20260908/docs/interop/FG_SAMPLE_PROBE_20260908.md)。
