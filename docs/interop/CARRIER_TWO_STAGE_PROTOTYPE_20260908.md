# #153 / #106 carrier 两阶段激活原型

本片从 `main@cd10d96b817d83ea4d84da4286c52cbdc0ca377a` 普通合入
`#175@e8e5fa06b5517d43388d76a2fcc99628f9c1df5f` 后建立。它只把候选合同写成
CPU/普通 Windows DLL 可执行原型；carrier 生成器和生产源码没有改变。

## 受限结论

当前固定 carrier 的 loader notification 仍在 loader lock 内枚举模块、解析导出、
记录日志并写入 Hook；`DllMain` 仍完成 ReShade 注册、配置与日志 I/O、NGX loader
选择、事件注册和首次扫描。这些是已读源码事实，不是 BG3 崩溃栈。现有附件没有
fault PC，不能把 #153 或 #106 宣称为已定位根因。`source=off` bootstrap 和模块枚举
越界已有独立修复，本片不重做。

选定的最小产品方向是两层准入：正常的 ReShade `init_device` 只在 D3D11 device 上
安装 Create/Release 观察 Hook，不创建私有 D3D12；只有同一 device 的 D3D11
`init_swapchain` 才允许完整 bridge。辅助 D3D11 device 可以被观察，但它自己的
swapchain 不会激活另一 device 的 handle。D3D12、Vulkan 和未收到 D3D11 device
回调的进程不激活。

loader callback 最终只能向固定容量 ledger 写入 base、load/unload 和单调 epoch。
满时置 `needs_rescan`；安全回调依据当前模块快照做全扫描。卸载或基址复用后，旧
load 事件不得触发 Hook。验证器必须接收完整事件并核对当前加载 generation/模块
身份；仅查询“该 base 现在有模块”会把 A 的旧事件误用于复用同地址的 B。扫描、
`GetProcAddress`、日志、Hook 安装/移除和资源释放
都留在 loader lock 外。

## 无法同时满足的合同

候选 device 出现后、观察 Hook 真正覆盖新模块前，NGX 可能立即调用 Create。
loader callback 若保持轻量，就不能在该窗口内同步补 Hook。因此严格“候选期零 Hook”
和“绝不漏任何早期 Create”不能同时保证。本方案接受候选 D3D11 device 期存在轻量
观察 Hook；若 carrier 晚载入或 late-loaded NGX module 在安全 pump 前已经 Create，
该 handle 没有可信 flags/结果，必须报告 `incomplete` 并拒绝 NR。确认 swapchain
不能反推或伪造这段历史。

本原型覆盖固定 ledger 溢出与 rescan、卸载后不使用陈旧 base、其他 API 不激活、
辅助 device 隔离、早期 Create 原生恰好调用一次、确认链前私有 D3D12 为零、同 device
确认后准入、Release 与 destroy 后退役。它尚未实现真实 14-byte Hook、ReShade 回调
并发、正在执行的 Create/Release 与 destroy 竞态、真实 carrier FreeLibrary 恢复、
多 swapchain 计数或 device/NGX/shared-resource 的完整退役。它不是游戏、NVIDIA NGX、
GPU 或崩溃修复验证。

初始原型提交 `4f056bbc37cf27167226fd4a693b4753afd5db29` 有两个覆盖缺口：它只把
base 传给 `is_loaded`，无法区分相同 base 的加载 generation；它还在调用原生 Create
前由测试传入预设 handle。后续修正加入 A 卸载、B 同址重载的反例，并让真实测试 DLL
通过 Create 的 out 参数写回 handle，观察层仅在成功返回后读取并记录。初始提交不能
作为这两项合同的通过证据。
