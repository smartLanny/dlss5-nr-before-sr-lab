# dev12 实机失败与隔离接续

本记录替代“等待游戏验收”的旧状态，不改写既有源码/编译/WARP证据。

## 已确认结果

- 维护者确认两游戏触发动作均为按 `=` 打开 ReShade/DLSS5 悬浮菜单。
  异环崩溃，2077无响应，升级前2077正常。
- 加载构建源为 `420a39ae021d45c06d397390535a01076bff14de`，中文Core SHA256为
  `0ffbc043057e9f42db59dc25f58b7c356b189557442490f60d285380d10e6fad`。
  ReShade版本为6.8.0.2155。2077卡死进程已结束，未在结束前采集hang dump。
- 两游戏Core已恢复到原dev11备份，SHA256为
  `16816aeaecdc585c8f059423d3cf328bcfc63f4306cf35e06cea5da531aa89aa`。
  恢复只改Core，当前INI、快捷键与运行库哈希核对不变。dev13从未部署。
- 原始日志、四份异环dump和恢复收据保存在私有证据区，不进入公共源码。

## 菜单问题的证据边界

异环最新dump的异常线程为RHIThread，UE断言报告D3D12Viewport.cpp:554，
HRESULT为0x80004004。当前可用dump没有对应游戏代码页，本地相应PE代码区域为零，
没有匹配PDB，不能把源文件行号直接解释为ResizeBuffers或GetBuffer调用。
ReShade日志只有启动期成功的交换链创建/重建，没有故障时刻的新ResizeBuffers失败。

该次异环完整Core日志无RR Create/Evaluate，dump模块清单无RTSSHooks64；
不重复要求维护者排除RR或RTSS。加载dlssg模块不等于证明FG已启用。
2077日志在停止前仍记录成功的SR→NR处理。两游戏同触发动作不等于同一根因已证明。

dev12新增原生Create登记，会让此前不由ReShade事件登记的内部DIRECT列表取得token；
后续真实Reset可以复用已观察的设备/接口族开始状态跟踪。ReShade自己的即时命令列表
也由原生Create创建，绘制内置菜单。此扩大的跟踪范围是当前隔离假设，尚未确认是卡死原因。
未删除Reset/状态/生命周期保护，未凭猜测修改厂商模块或吞异常。

## RR深度的独立修正

新F8窗口：depthAdapter.recorded=0→0、depthAdapter.declined=96→97、
prepareDeclined=98→99。窗口内仅1次新拒绝，没有转换shader执行记录。

固定ReShade源4a50d1e的resource GetDevice钩子可能返回设备代理，而原生命令列表
返回底层设备。旧source_device==device能误拒同一设备。隔离补丁通过固定
IID_UnwrappedObject和标准IUnknown身份比较，仍拒绝不同/无法证明的设备，
不替换实际执行设备，不放宽格式、投影、资源预算或fence条件。
CPU夹具提取生产helper，覆盖代理与原生、两代理同底层、不同底层、查询失败/null、
引用计数平衡，并用旧指针判断作负控；这不是游戏运行证明。
F8增加lastReject与proxyDeviceMatches，日志在拒绝原因变化时输出具体阶段。

隔离补丁构建源为`7177b78c5b7e02ebc29f21897ec4a8afbc3c5c9d`。
24项无GPU检查及固定ReShade源的双语完整Core/nrchain编译链接通过。
source-manifest SHA256为`1681e056f53c08f3cabcec82b352c5a6c5b55cd2946c9cb82177d7951fa260b6`，
validation.json SHA256为`658b48da4ec9850f4078e52fc2c659aae9568a10bdf95c8b1bf11794c8626ed0`。
这只是私有编译检查，仍保留dev13版本串；没有生成新验收包或替换游戏。
本补丁WARP/NVIDIA/游戏/性能均未验证，继承的菜单回归仍未解决。

## 后续

先在私有受控ReShade窗口检验菜单绘制假设，得到有效复现后修正最小边界。
RR修正与菜单结论分别验证。#206/#207不作为通过验收的发布输入；#99/#101开放，
#162人脸按维护者要求继续暂停。安装器任务已同步暂停集成dev12。
