# Two-RR approximate v1 窄核

仅核 self-owned 私有host差异；没有产品源码变更、没有 GPU/NGX 运行。固定 R=N=1280×720/O1920×1080，单层 NR，三条独立32帧历史。CPU 编译和14项算法/位型反例通过，不能替代 shader/GPU/RR 验收。

请检查：

- 私有 child 复用同 original Core/device/init domain，官方 R→R DLAA optimal 再验证；child 与原始 R→O parent 独立 history，无第二次 native Init。
- 原始 parent 的 non-Color getter 合同仅在同进程前后比较；跨进程使用 normalized raw contract，不能比较包含资源地址的 SHA。
- NR source 改为 clean，color jitter=0，depth/MV 沿用 raw 明确 approximate；不冒称 exact P0 visibility。
- clean/edited/composite 三资源独立；descriptor register/format一致；原始真实 lease/recording/queue-fence/Reset/opaque capture-restore 保持，child+composition packets 追加同一 recording 的精确退休。Plan.retained 固定只有2槽，第三readback已由child packet保留，不能越界扩写。
- raw→composite 先 CopyResource；identity/拒绝 shader return不写。有限非identity编辑域限定 raw alpha=1，其他 alpha 退回。正权双线性逐tap ratio-before-shift，-j符合冻结raster s=-j。拒绝域和 CPU oracle 是否一致。
- 首失败 quarantine/保留闭包，无重试、无 fallback、无提前 Drop；生命周期 stress 中 vendor partial-handle/device loss/failedReset 仍 unknown。

源码/对象身份在 SOURCE_HASHES.json；HLSL和新增approx模块独立编写，child方法来自本项目所有的已测冻结host。不含 GPL 参考实现、vendor SDK、二进制、原始日志或真实绝对路径。GPU执行仅交 root 在窄核后串行进行。
