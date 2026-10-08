# Two-RR approximate v2 最小修复窄核

仅核 self-owned 私有host差异；没有产品源码变更、v2没有 GPU/NGX 运行。v1实际frame0 Close80070057，三次Eval只是录制API成功而未Submit，new帧输出0/退休0。先看V1_TO_V2.diff：确定修复Move同态transition；独立补齐21槽typed-null descriptor布局。14算法/位型反例与7源码导出CPU录制/布局控制通过，不能替代真实D3D12/NGX验证。无重试/放宽guard/算法或NR参数改变。

v2只新执行twoRR分支32帧/34个recording，两个控制臂带原v1 EXE身份显式继承已固定actual+独立CPU review，不覆写旧结果或伪称同binary。BRANCH_CHANGE_SCOPE.json证明除approx_resources外host源码/共享oneRR控制路径与v1逐文件相同；比较使用normalized raw合同SHA，原地址相关nonColor SHA只在各进程内部比较。

请检查：

- 私有 child 复用同 original Core/device/init domain，官方 R→R DLAA optimal 再验证；child 与原始 R→O parent 独立 history，无第二次 native Init。
- 原始 parent 的 non-Color getter 合同仅在同进程前后比较；跨进程使用 normalized raw contract，不能比较包含资源地址的 SHA。
- NR source 改为 clean，color jitter=0，depth/MV 沿用 raw 明确 approximate；不冒称 exact P0 visibility。
- clean/edited/composite 三资源独立；descriptor register/format一致；原始真实 lease/recording/queue-fence/Reset/opaque capture-restore 保持，child+composition packets 追加同一 recording 的精确退休。Plan.retained 固定只有2槽，第三readback已由child packet保留，不能越界扩写。
- raw→composite 先 CopyResource；identity/拒绝 shader return不写。有限非identity编辑域限定 raw alpha=1，其他 alpha 退回。正权双线性逐tap ratio-before-shift，-j符合冻结raster s=-j。拒绝域和 CPU oracle 是否一致。
- Move先实际observer解析state，再if(state!=wanted)发transition，显式UAVbarrier未删；旧实际源码CPU反例必录制64→64，新同态序列跳过no-op且保留真实转换。微软runtime明确要求before/after不同，违规Close E_INVALIDARG；参见README和CPU_RECORDING_CONTRACT.json引用。
- codec root声明13SRV+8UAV；v1实际DescriptorSet只有14槽/10个Empty，v2完整typed-null初始化21槽再覆盖4个实际view，并验证真实heap及packet plan。布局问题独立成立，未将其指定为v1首因。
- 首失败 quarantine/保留闭包，无重试、无 fallback、无提前 Drop；生命周期 stress 中 vendor partial-handle/device loss/failedReset 仍 unknown。

源码/对象身份在 SOURCE_HASHES.json；HLSL和新增approx模块独立编写，child方法来自本项目所有的已测冻结host。不含 GPL 参考实现、vendor SDK、二进制、原始日志或真实绝对路径。GPU执行仅交 root 在窄核后串行进行。
