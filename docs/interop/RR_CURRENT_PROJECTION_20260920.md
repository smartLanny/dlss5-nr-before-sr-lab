# RR 当前投影准入与缺投影后置候选（#254 / #226）

接续 unified3 `f82930dd`，候选身份 `beta0.5-dline21-unified3-rr1`。
**#254 仍开放。没有鸣潮开启 RR 的修后 F8，不能标为已修复。**
本批按维护者范围验证本地 CPU / WARP；不安装游戏，不发布稳定版。
旧 unified3 已交付包不包含本补丁，不能用它验证新增开关。

## 输入证据与判断

已读 #254、#226、[异环先例](DEV12_MULTIGAME_20260909.md)和维护者
9 月 20 日的修前 F8（D21 223fix2 / 4080 SUPER）。私人原件留在证据区。
`current-projection`、`rr-safe-bypass`、`nrSuccess=0` 重现；
`projection.state=1` 表示本次参数查询没有可复制的矩阵，
并非“取得有限矩阵后数学检查过严”。`proxyDeviceMatches=2856`，
不重复套用早期设备代理修正。矩阵细节尚不存在，不能推测转置、近裁面或单位。

NVIDIA 的 [RR 参数定义](https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_defs_dlssd.h)
将线性与硬件深度分别声明，矩阵键为 `ViewToClipMatrix`；
[评估入口](https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_helpers_dlssd_d3d.h)
通过当前参数块传入矩阵。没有用 FG 相机、旧帧缓存或游戏名替代当前 RR 相机。
异环当时有可复制的正向 view-Z 矩阵，情况不同。

## 实现

- 同步复制当前 Evaluate 的矩阵，统一诊断与实际准入的读取边界。
  将缺失、不可读、非有限、不支持、可转换明确区分；保留既有正向／反向、
  有限／无限远转换，不扩大投影数学白名单。诊断有限拷贝仍不代表准入。
- 缺矩阵且实验关闭：保持原 `current-projection` / 原生 RR 旁路。
- 新实验页开关 **RR 缺投影时尝试后置 NR**，INI
  `RRMissingProjectionFallback=0`。仅对矩阵缺失、Create 明确声明线性深度
  的 RR 尝试。不可读、非有限或不支持的已提供矩阵仍拒绝，开关不放宽这些条件。
- 实验使用独立 R32 已初始化远平面，占位深度标记进入存储签名；
  **不是推算、转换成功或真实硬件深度**。仍保留合法原生运动数据，
  每一层实际 `DLSSNR.Reset=1`，不跨帧积累未知深度历史。
- 沿用 slot / 描述符 / 完整设备身份 / 格式能力 / 生命周期 / 命令状态 /
  队列 / fence / 预算保护。原生 RR 的输入、输出参数及一次调用合同不变。
  无法准备时保留原生 RR；不在不可信录制上补算或强制释放。
- 逐帧占位 Reset 与创建、游戏 Reset、配置／颜色／导引变化的预热分开。
  后者保留原预热和隐藏重置帧行为；否则候选会永远停在预热，推理成功也无可见采用。
  `diagnostic_reset_requested`、实际参数和捕获仍记录真实 Reset，
  `successful_evaluations_since_reset` 在该候选中表示最近一次实际转场重置后的
  预热成功数，不代表历史累积长度。F8 新字段明确逐帧 Reset。
- 两种深度模式切换改变存储合同，进入既有实例与独立子实例退休；
  关实验不提前释放在途资源。独立人脸跟踪不将占位深度标作有效深度。
- 比例修改和画质重置不暗改该兼容许可；采集占用时拒绝修改。
  不改变模型强度、颜色算法、层数或工作比例；可能闪烁或改变细节，需要游戏验证。

## F8 判读

| 字段 | 含义 |
| --- | --- |
| `post.rr.depthAdapter.currentInput` | 最近一次实际线性深度入口读取结果，非诊断缓存 |
| `post.rr.depthAdapter.projectionAdmitted` | 合法投影和输入存储准入累计次数，不是 GPU 完成 |
| `post.rr.depthAdapter.recorded` | 真正投影转换的累计录制次数；不计占位初始化 |
| `post.rr.missingProjection.requested` | 独立实验保存请求 |
| `post.rr.missingProjection.admitted` / `.recorded` | 占位候选准入／初始化录制次数；按层计，不是帧数 |
| `post.rr.nrRecordings` / `nrSuccess` | 结合同进程增量核查后置采用与推理；单看请求和占位次数不算有效 |

读这些计数时同时检查 `depthType`、实际路线、失败原因、配置代次和时间窗口。
累积计数不能证明当前帧已采用；录制不等于 GPU 完成、显示或画质通过。

## 本地验证与范围

- 实际 `ResolveRrLinearDepth`、实际 Reset / 预热块：Windows O0 / O2 通过。
  包括修前缺失条件、默认拒绝、显式占位、矩阵恢复、拒绝旧矩阵复用、坏指针、
  非有限、斜投影、不支持存储和五层逐帧 Reset。真实游戏 Reset / 历史变化仍重新预热。
- 原投影数学、COM 设备身份、RR 控制流及 F8 回归通过；67 项实际配置／占用／
  INI／撤销许可／诊断序列化检查通过。原生 RR 恰好一次和失败分支测试保留。
- 实际生产深度 shader WARP：10 场景通过。8 个原转换场景，2 个占位场景；
  R16 / R32、子区域、NaN、负值、原深度逐字节不变及私有输出读回检查。
- 核查最终生成和 Windows Core 另记结果。NVIDIA、鸣潮修后 F8、游戏画质与
  RR/FG 共存未验证；本地通过不关闭 #254。

## 鸣潮最短验收（候选构建后）

保持相同 RR、FG、模型、层数与比例：实验关闭记录一份 F8，开启后等待处理稳定，
再记录 F8；确认原生 RR 一直保留、转换与占位分开计数、NR 实际采用增加。
转动镜头查看闪烁／拖影／细节；关闭实验，确认不再增加新的占位和 NR 采用。
需要同时提供本次 Core 版本及哈希。若仍旁路，按新的首个拒绝点继续修，
不把单测或占位录制冒充鸣潮已修。
