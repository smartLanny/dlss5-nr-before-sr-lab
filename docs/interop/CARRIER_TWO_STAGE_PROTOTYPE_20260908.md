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

## 最终源码、构建与验证身份

修正后的原型源码提交为 `4dc614409e4c367d302ff375a026f5ffefcdf5ea`。该提交只含
本文件和以下两个测试源码；没有 carrier 生成器或生产文件：

| 文件 | SHA256 |
|---|---|
| `tests/native_bridge/carrier_activation_contract.h` | `901af918c3e16372f2c3262717e4d7a4b6d088c6920d1e2da38ba231786f089a` |
| `tests/native_bridge/carrier_activation_contract.cpp` | `4fa3a28721fa4ca3ca4af3a2e546389b6e2dfb3c1c001cd4f7111df335079ab0` |

普通 Windows DLL stub 和程序只保存在忽略的 `build/carrier-activation-prototype/`，
不会随 PR 发布：

| 本地验证产物 | SHA256 |
|---|---|
| `carrier_activation_contract.exe` | `d0c457879e7560d5b2f04d0e701064242815ff472117467e7b7e8860295fac95` |
| `ngx_stub.dll` | `4785cb16f05b508dc5b6fc223eea0aa677b3b5618fd8f9fecd43aec5e0e8384d` |
| `forward.exe` | `4649e3e01c0bf229ff5e41e12d214984ffeff3e16948342f71b5c1b0438d5fac` |
| `verification-output.txt` | `2376e337d863ccc0accb4153c3cc94a392bc16899c54d8b4d8968400d741b1c6` |

使用已安装的 VS Build Tools 2022 x64 环境。以下是实际的编译和最终执行形状，`Repo`
为本隔离工作树，`Build` 为上述忽略目录：

```powershell
call "C:\BuildTools\VS2022\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
cl /nologo /EHsc /O2 /LD "$Build\ngx_stub.cpp" /link /OUT:"$Build\ngx_stub.dll"
cl /nologo /std:c++17 /EHsc /O2 /I"$Repo" "$Repo\tests\native_bridge\carrier_activation_contract.cpp" /Fe:"$Build\carrier_activation_contract.exe"
cl /nologo /std:c++17 /EHsc /O2 /I"$Repo" "$Repo\tests\native_bridge\forward.cpp" /Fe:"$Build\forward.exe"
& "$Build\carrier_activation_contract.exe" "$Build\ngx_stub.dll"
& "$Build\forward.exe"
```

最终控制台输出为：

```text
carrier_activation_contract.cpp
carrier two-stage activation contract: passed (prototype only, no GPU)
native bridge exact forward body: passed (CPU doubles, no GPU)
```

原执行没有把 stdout 同步重定向到文件；`verification-output.txt` 是随后逐字保存的上述
三行终端输出，哈希证明这份转录，不能冒充同时生成的原始日志。DLL stub 确实经
`LoadLibrary` 加载，并通过真实 out 指针返回测试 handle；它没有真实 NGX、14-byte
Hook、ReShade callback 或 GPU。`forward.exe` 验证现有生产 Forward fixture，但不是
B 原型已经接入生产的证明。没有 NVIDIA/game 结果，也没有 #153 或 #106 的 fault PC。
