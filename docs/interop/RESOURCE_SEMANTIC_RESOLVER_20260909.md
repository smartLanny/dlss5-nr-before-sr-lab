# #2 / #203：统一资源语义结果

本片将初始 native-view 接线收敛为 `ResourceSemanticResolver::Resolve`。
生产准入与 F8 使用同一个结果；Color、Output 的最终颜色/曝光条件也在实际
资源 adapter 中完成，slot 创建、命中和重绑再次检查相同合同。

选择顺序为 typed/observed Exact、Unique contract、ambiguous 原生回退。
Unique 的候选集合包含所有合法格式族解释。只有已声明合同或成功的设备能力
查询可以排除候选；尚未实现的 sRGB/SNORM/整数 codec 不能用来制造唯一性。
未查询和查询失败保留未知，缺失 Create flags 不冒充已声明的 LDR。

原有前置 FP16/FP32 FLOAT 存储 adapter 被明确标为 scene-float contract；
不能据 HDR 标志自动推断所有资源均是 FLOAT。typed FP16 后置的 CopyResource
快照保留其免读取原 Output 的例外，该例外不适用于 R8 或前置输入。

Exposure 沿用共用读取器作为独立 role adapter，并发布相同结果结构。F8 能直接
区分 #229 的 typed R8 最终曝光尺度、失败的设备查询、尚未进行查询，以及
#202 LDR 中不使用的 HDR 曝光纹理。原生参数和已有曝光数学不变。

F8 每个角色记录 storage/flags/shape、观察到的全部有界 view、来源、候选与
合同/设备筛选结果、具体缺失证据和 nextAction。`admit` 表示资源合同准入，
不单独证明模型、提交或最终画面；恢复状态还须由对应尝试的实际运行证据决定。

这仍是集中候选的内部实现提交。后续接入同进程恢复账本、Compatibility
Verdict 和实际写入后读取证据，完成统一同源门禁之后才交付一个候选包。
