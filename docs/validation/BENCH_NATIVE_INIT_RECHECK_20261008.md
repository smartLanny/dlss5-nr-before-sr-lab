# 官方测试台无注入初始化对照

2026-10-08；维护者重新授权继续探索，旧 r7 失败和当时 STOP 保留。本轮由
GPT-6.1 Sol xhigh 实现 CPU 对照，GPT-6 Astra high 独立审查后，root 只执行一次。

## 实际结果

固定 NRDSample/NRI、310.9.1 静态 SDK 和 SR 预载、Agility、ProjectID/CUSTOM、
可写数据目录、CommonInfo/callback、API 0x15；只移除 ReShade/Core 注入环境。
这属于环境组对照，不是严格单变量根因实验。

实际选择 RTX 5090 D，进程内 NRI adapter UID 与 typed D3D12 device LUID 相等；
Init 前完整模块门确认没有私有 dxgi/ReShade、Core、Feature18 或 nrchain。
227 个冻结文件和实际 COFF→PE 由 Astra 重算核对；驱动 NGX core 前后哈希相同。

唯一 Init 仍返回 `0xBAD00001`；日志首先在 `NGXCheckIfSupportedOnHW:130`
报告 unsupported hardware。Init 未成功，因此 Shutdown=0；caps/Create/Eval/NR
全为0。进程 PID19408 正常结束，Sample 的 exit1 是预先设计的提前停止，不能
当作 Init 成功。runner exit0 只说明这次有界实验与收据完整。

因此，单凭 ReShade/Core 注入不能解释此失败；但仍不能确定具体的 SDK、
Agility、宿主状态或驱动内部硬件识别原因。没有扫描运行库、驱动或环境绕过。

| 证据 | SHA256 |
|---|---|
| 冻结 CPU packet | `976ab852a5fe6eebbe35f2fee11091beaa35cb1bf4766216ae71576037a8d72d` |
| runner | `b199f286354f9d9e3d5ce1a38899023288437b20aebb1877fa57e7f7f4ba0add` |
| 实际执行 receipt | `b74729ce396363a5605193cb769a8bbef2c248bea6d32aa03f48512cb230d3fc` |
| 实际 stdout | `e6a6d7e56eb8a04169d2718a11500491adf6172c06db5f948abcfac571f803dc` |
| driver core 前后 | `91d3742f0df3f2dd85141fa39ea257f7b9243c3e6755cd7fea894c721e7491aa` |

## 选择的替代方案

封存这个 310.9.1 Sample 初始化分支，转复用已经实际成功的 310.5 host 及其
完整同源当前 Core 实现、真实 Feature18、native RR 和精确退休。它编入完整
Core TU/准确对象，不冒称候选 addon DLL 已载入或产品默认路径已验证。

新增有限静止 3D 房间、两个不透明盒和薄几何；同一 jitter primary ray 的真实
交点产生线性 HDR 颜色、硬件深度、世界法线、线性 albedo 和 roughness。
面积光采样与真实遮挡产生噪声；不以 RGB 白噪声替代。camera/几何静止，MV=0，
jitter 独立提交；漫反射材质无 specular lobe，specular albedo 精确0，roughness1，
specular MV 的空信号语义明确，不伪造 hit distance。

先保留生产 warmup 两帧，第三帧证明实际 NR combine→一次 RR 输出采用和精确
退休；首门未通过前不开性能。之后画质采集与性能测量分开，相同镜头、输入、
seed 和预热；单池资源在真正 fence+Reset 后复用，性能关闭完整画面读回/poison。
同队列 GPU timestamp 和进程内 DXGI 显存记录采样峰值、稳定值、退休后的变化。
本段是实施方向，不是已经完成的 MVP、画质或性能结果。

RTXPT 增加 Donut/Streamline 与新接缝，当前不选。没有另建大型 renderer 项目。

## 一手资料与限制

- [官方 NGX Init 合同](https://raw.githubusercontent.com/NVIDIA/DLSS/main/include/nvsdk_ngx.h)
- [官方 RR 输入合同](https://raw.githubusercontent.com/NVIDIA-RTX/Streamline/main/docs/ProgrammingGuideDLSS_RR.md)
- [NVIDIA 论坛相同错误阶段的一手复现](https://forums.developer.nvidia.com/t/ngx-d3d12-init-fails-with-0xbad00001-no-matching-adapter-found-on-dual-rtx-system-all-luids-verified-correct/384236)：仅提供排查方向，没有 NVIDIA 根因定论，不证明本机同因。

产品默认仍 RR→NR。有限 3D host 的 NR→一次 RR 是独立研究路线；它既不代替
两 RR 实验，也不构成游戏 FPS、质量或候选 addon 验收。已部署组合候选及游戏
运行库、配置、回退均未改变。
