# 同源3D twoRR v1b 仅解析修正末核

原v1没有GPU运行；唯一阻断是CPU收据错误查找phase_J[3]，真实冻结数组一直为32行。V1_TO_V1B_RECEIPT.diff只将该一处标记修为phase_J[32]。CPU_PHASE32_PARSER通过实际旧AST表达式证明必IndexError；新composition/scene两个实际解析表达式对真实数组的前三行完全相同且s=-j，48byte Scene constants逐帧固定。原manifest完整保留，native host、EXE、两CSO、scene、runtime/source/guard均未改变或重新编译。全部已有runner/helper闭包审查继续适用；请核解析修正、来源映射及新manifest身份。新增CPU反例仅解析文件，不创建进程。

唯一root-run仍为一个新进程2warm1observe、5recording、4Scene dispatch，固定fresh输出minimal-scene-two-rr-v1-r1；不重试、不kill。第三帧必须实际combine、预测与实际RGB都非零、变化仅在oracle域、值<=原1FP16ULP、全部alpha与non-edit完整bits相同；任何失败停止。没有放宽任何GPU输入、算法或收据门。画质、开销和游戏unknown。本目录仅自有sanitized源码与CPU证据，不含vendor、binary、原始日志或真实绝对路径。
