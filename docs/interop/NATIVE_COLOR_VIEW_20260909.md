# 原生 Feature 1 视图与 R8 存储合同

本片接续 #2、#202，基于 output.2 底座。TYPELESS 资源的物理格式不再直接作为
SRV/UAV 格式；准入、slot 创建、同资源命中、重新绑定和所有描述符重建都使用
同一解析结果。没有按游戏名选择的分支。

## 准入来源

- 原有 typed FLOAT 和明确的 FP16/FP32 FLOAT 存储合同保留。
- R8 TYPELESS 必须取得可归属到成功原生 Feature 1 调用的具体视图。
  原生设备 SRV/UAV hook 完整转发一次，再只记录当前调用的 Color SRV 或
  Output UAV；插件自身的编码/描述符工作不建立原生证据。
- 记录以匿名设备、资源生命周期和 Feature generation 为键，不存 descriptor
  地址，不把资源地址复用当作旧资源。COM private data 调用位于 registry 锁外；
  固定 256 项账本只保存值，淘汰后旧键未知。
- 本候选仅接通 R8 UNORM 的有界数值域。sRGB、UINT、SNORM、冲突、失败调用、
  不合法的视图形状继续拒绝；设备能力仍查询具体 typed view。
- 观察器只覆盖成功原生调用同线程创建的视图。预先缓存或工作线程创建而未被
  观察的视图仍未知。创建证据不是 GPU 消费或最终画面证明；首个未观察的调用
  可以保持原生，后续相同生命周期取得证据后才准入。

设备 hook 复用现有序列化 publisher，独立提交两个有界请求；安装先于 chain
和交易锁。每层 TLS scope 保留父级，失效嵌套调用遮蔽父级。原生返回值、参数、
实际调用次数不因观察而改变。

## 前置与后置

前置只读取 Color，原资源无需 UAV；输出继续使用自有 FP16 纹理。在已有 LDR
合同下，以相同的有界编码数值进出模型，不额外猜测 gamma。后置还要求合法
UNORM UAV、ShaderLoad/TypedStore 能力及明确的 LDR 合同，真实 Output 先读入
自有 FP16 快照，再将变化写回原输出。已有手动输出编码选项保留。

LDR 的 ExposureTexture 属于 HDR 专用参数，不应单独否决 LDR 准入；检查只在
临时合同副本中忽略它，原生参数保留。明确非 unity 的曝光标量仍拒绝有界 R8。
HDR 及原有 PQ 检查不放宽。

依据为 NVIDIA [DLSS 编程指南](https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf)
3.1.2 的 LDR 数值域和 3.9 的 HDR 曝光合同。指南未提供通用的
`R8 TYPELESS → UNORM` 推断表，因此资源格式本身不是准入证据。

## 当前验证与交付边界

修改后的真实生产 ledger/runtime/hooks 宿主已完成 1,134 项检查；LDR/曝光及
原生参数获取实际函数完成 29 项检查。历史完整格式生成输入及当前源码接线
检查共 8 项通过，无跳过。完整转换后的 Core 已通过语法编译。

这些结果不包含 NVIDIA 模型或游戏。集中候选仍须完成同源构建、生产
CreateSlot/Rebind/描述符与 shader 的 WARP 对照、双语菜单，以及维护者实际
游戏中的 NR 处理、F6、运动和退出验收。原始报告与阶段性构建保留在私有证据
目录，相关 issues 在游戏验收前继续开放。

F8 分别报告 physical、SRV、UAV、来源、匿名生命周期、Feature/config、hook
覆盖、冲突和首个未知状态；`viewStatus=ready` 仅指视图访问，不等于 NR 准入。
