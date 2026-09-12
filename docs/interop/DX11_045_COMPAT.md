# DX11 0.4.5 exp3 — 兼容迁移与分组验收

## 事实与基线

主人已确认 BG3 DX11 beta 测试通过，#80 已记录、#58 已关闭。保留该正例，不再把 BG3 写为从未生效。该次精确测试包 SHA 尚未钉定，**不能把这个正例自动升级为本次 exp3 或全部 DX11 游戏通过**。

本候选基于 `release/0.4.5-test@61fe9a0fe527fb2304c382c332cbe62876228b8b`（0.4.5-fix1 源码）；包含已发布 0.4.5 的维护修复及 #125 后续的列表轮换/状态/UI/F8 修正，不含 0.5 算法。已发布 tag `beta0.4.5` 仍为 `d403388`，不移动。先叠在 #117 的隔离集成线上，最终归入 `release/0.4`；不修改 main 或发布新版 Release。

复用 #79 的薄 consumer/client/forward/history 与桥私有 PSO/root 基线。上游 transport 固定 NIGos `29baa4cc56e0aeb21219de16528e697b91522efc`，不是宣称升级了它的全部最新版本。没有引入 #120 的另一组 D3D11 NGX hooks，避免和配套桥重复截获。

## 本轮实际修改

1. **追上 0.4.5-fix1 核心**：原生透传、列表观察、NR 释放失败隔离、强度上限4、恢复推荐、独立 F8 TXT、稳定状态栏与快捷键规则保留。不是把旧核心重新贴版本号。
2. **#94 的 active extent**：从当前 Evaluate 读取实际 R，建立私有 SR Feature 时跟随该 R，而不是继续使用过大的历史 Create 输入。Color、Depth、MV、Output 的原点各自校验，保留奇数尺寸和留边 allocation；不猜测交换输入输出、不用事后缩图冒充游戏真实渲染缩放。缺失/矛盾合同安全回退。
3. **局部输出回填**：实际 CopySubresourceRegion 使用 Output subrect base 作为源和目标原点，只覆盖合法 O 区域。非零原点也视为局部输出，继续先执行原生 SR，保留外部像素。修复旧桥固定0,0的写回。
4. **Create flags 按句柄保存**：当前 Evaluate 未重复 flags 时使用该句柄的已知 Create flags，不把未设置猜为107；切换不同 flags 时私有 SR 重建。缺少可核验 flags 继续拒绝，不假造 HDR、深度或 MV 网格。
5. **部署诊断**：每秒有界模块文件名观察。配套桥缺失/不匹配、重复桥、带数字版本后缀的通用 RenoDX DLSS5 NR 有独立提示。通用 NR 冲突沿用现有进程级旁路；Luma、特定游戏 RenoDX HDR 和普通 NVIDIA DLL 不按通用 NR 判冲突。枚举失败不证明模块不存在，文件名存在也不证明 GPU 工作成功。
6. **真实 R UI 保留实验语义**：仍是厂商 optimal-settings 建议覆盖，默认0=游戏控制，需引擎重查。NRScale=N/R 和 RenderScale=R/O 不混用；固定 DLAA、静态/缓存回调或 DRS 不保证响应。未提高当前50–100%范围，不把未经验证 HDR/RR/FG 作为启用前提。

## 验证等级

本地执行的生产矩形/回填/名称识别 C++ 函数通过 GCC 和 Clang ASan/UBSan；包含逐像素输出边界与旧0,0回填的负对照。这些是 CPU 合同回归，不是游戏截图或 NVIDIA 测试。

新增 Windows 工作流会检查真实固定上游补丁、完整中英文核心/桥编译、历史/转发/协商测试及实际 WARP helper。**只有当前 PR 精确 head 成功才可以领取新测试二进制**。本次曾遇到 hosted Actions 未启动步骤、无构建日志，不能沿用旧 #79 CI 给 exp3 放行。

没有云端目标游戏/NVIDIA GPU 实测，不承诺所有游戏、无崩溃、净提帧或 MFG 已通过。保留 #79 的同步实现，CPU 释放游戏等待不等于真实 GPU 完成；stall/fence_progress/资源回收、跨 API 长时运行仍属于阻塞验收，不因矩形测试通过而跳过。

## 一次安装、按输入类别并行验收

以下为测试分组，不是已支持名单。每个游戏只需先完成“提供者单独 → 同版本 NR 关/开 → F8 + 画面对比”这个小闭环；成功后再调 N/R，不要求普通玩家跑全部技术矩阵。

| 分组 | 代表对象/证据 | 当前状态 | 新包重点 |
|---|---|---|---|
| 原生 DX11 DLSS | BG3 | 旧 beta 主人已确认通过 | 升级回归、角色创建/读档、N/R与输出区域 |
| 引擎 TAA→D3D11 DLSS | Prey/Luma、Skyrim 的既有 DLSS 模组、奥德赛类提供者 | 外部先例/候选，不是本项目已实测 | 原生 guides、仅一个 NR owner、各自 subrect |
| Create/Evaluate 不同 | #94 PSO2/ngxGym 几何样本 | 上游合同证据 | 只用离线合成 fixture 验证尺寸；不建议受保护联网游戏注入 |
| D3D12 原生/外部时域提供者 | 0.4.5 现有支持路径、FSR/XeSS→DLSS 外部提供者 | 按每个游戏证据分开 | 升级无回退；没有新增原生 FSR/XeSS adapter |
| Feeder/Synthetic | ReShade 成品画面+估算运动 | 另行隔离候选，未集成本项目合成路径 | 只省 NR 成本，不称降低游戏 R；HUD/曝光/时域数据缺失单列 |
| FG/MFG 共存 | #126 CP2077、#63 RTX40MFG | 仍待明确日志/实测 | NR关/开 × FG关/开；官方和第三方分开，不加控制倍数 |

#118 鸣潮旧 DX11 包缺配套桥/并有另一 NR 的记录是部署问题线索，不等于已证明该游戏可用。#121 AC Shadows 间歇闪烁、#123 Starfield 复杂状态、#122 NBA2K26 无 F1、#127 NBA2K25 等待 Reset 也不因为接桥就完成，不能和 #68 NBA2K27 混为一案。

## 包和操作

一包一个语言核心 + `dlss5-native-carrier-045-exp3.addon64` + 自有 `nrchain_nvngx.dll`。不含 NVIDIA/模型/ReShade/其它模组。默认 INI 放在 `defaults/`，不会覆盖旧配置；用户保留合法所需运行库。不要混装旧桥/旧核心/两个语言核心或同时运行提供者自己的 NR。离线、允许模组的游戏使用，不绕过反作弊。

已有成功包保存在游戏目录外以便回退。首次使用保持游戏自有 R、N=100%，确认 NR 与效果提交、F6 可见差异；再改 N；最后尝试 R 并观察实际输入和固定 O。F6 不会瞬间回滚引擎已分配 R。旁路时用 F8 TXT/ZIP 上传 BUG；矩形问题可附同次 `dlss5-bridge.log`。图像异常才补 F7。持续异常立即回退，不反复试崩。

## 合并门槛

源码审查、完整当前 CI、BG3升级回归，至少一个不同输入提供者或不同资源/格式合同的离线游戏实测通过；按代码影响范围给出有限支持结论。新增游戏要有精确 core+carrier SHA、提供者/运行库、R/N/O 与 active rect、NR/提交增长及F6证据。真实 R、FG节奏和端到端帧时间是独立勾选项，不能由“能进游戏/帧数增加”替代。

## 一手来源

- #79 / #80 / #94 / #95 / #118 / #120 / #126；0.4.5集成 #117/#125。
- 固定上游： https://github.com/NIGos/dlss5-bridge/tree/29baa4cc56e0aeb21219de16528e697b91522efc
- #94实验： https://github.com/NIGos/dlss5-bridge/commit/6da5b961460d498f68106d87214b7d90a08437cd
- NGX公开辅助接口： https://github.com/NVIDIA/DLSS/blob/a291cc7d2cc642a51566f3dfd5376f635cd1b284/include/nvsdk_ngx_helpers.h

未修改 NVIDIA loader 私有表；#95 驱动/snippet/consumer 差异只作为诊断维度。Feature18 保持 N→N；不追踪其原生 Upscaling，不合并未许可第三方算法。
