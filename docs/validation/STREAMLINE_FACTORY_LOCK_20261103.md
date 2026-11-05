# Streamline 工厂回调：锁顺序与 PCL 身份验证

基线：`53f06b518839226fefd1d8db46a50dddb4b569c6`。
关联：[#164](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/164)、
[#167](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/167)、
[#293](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/293)。
前置研究见 [UPSTREAM_DELTA_20261103.md](../research/UPSTREAM_DELTA_20261103.md) 第 4 节。

本次交付是**生产代码的锁顺序修正和源帧身份边界测试**。没有 Windows loader、
真实 MinHook 指令修改、Streamline SDK、GPU、NGX 或游戏运行结论；不关闭上述议题，
也不把它称为首次 NR Create 停顿或 FPS 优化。

## 1. 复现了什么

`nr_before_sr.cpp::OnPresent` 和 `SetStreamlineEnabled` 会进入 `Poll`；后者取得
`install_lock` 后调用模块查找、导出解析和 MinHook。已安装的 `HookGetFunction`
在另一线程取得 `slPCLSetMarker` 后，旧代码还会阻塞获取同一把锁并安装 marker。

测试包含完整的生产 `nr_fg_streamline.inl`，控制以下交错：

1. 线程 L 持有测试模拟的 loader lock，准备查询一个已经加载的 feature callable。
2. 线程 P 进入生产 `Poll`，取得安装锁，在模块查找处等待 L。
3. L 进入生产 `HookGetFunction`。旧实现等待 P 的安装锁，形成闭环。

恢复基线中的三条阻塞安装语句后，双线程用例确定报出
`factory/loader install-lock cycle`。另一个用例在已持安装锁的调用上下文进入工厂，
确定报出 `recursive install-lock wait`。测试以有界等待代替永久挂死；没有让
`GetProcAddress` 替身偷偷调用工厂，也没有声称实际导出解析必然发生这种重入。

这是可执行的锁图证据，前提是宿主在 loader 回调上下文请求已经加载的函数。
它不证明某款游戏已经发生死锁，也不保证任意 SDK 调用可以安全放进 `DllMain`。
Microsoft 的[DLL 最佳实践](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)
明确描述了持有私有锁后调用模块查询、另一线程在 loader 回调内等待私有锁的反序问题。
本仓修复删除自己的反向等待边，没有引入 `LdrLockLoaderLock` 或手动 loader 锁。

## 2. 生产修改

### 工厂只登记，Poll 继续拥有全局钩子

`HookGetFunction` 保留原始返回值和原生函数指针。首次发现 marker 时，先以实际
函数地址固定 detour 和目标模块，再用原子 CAS 登记目标，提前下一次 Poll。
回调不获取安装锁，不创建或启用 MinHook，也不等待其他线程安装完成。

下一次取得安装锁且 NR 允许工作的 Poll 使用原有 `Install` 完成全局 detour。
即使四个公共导出已经安装完毕，待装 marker 也不会被原来的 ready 快速返回漏掉。
一次函数查询就足够；安装失败保留既有一秒重试节奏，重复查询不会造成每帧重装。
若工作关闭或安装锁忙，安装继续推迟；不承诺固定一帧内完成。

没有改成只替换 factory 返回指针的 wrapper。全局 detour 必须覆盖**此前缓存的
原生函数地址**，否则 begin 经 wrapper、end 经旧地址时，TLS 帧身份可能一直残留。
本次 fixture 以全局入口跳转和独立 trampoline 模拟 MinHook 合同，实际调用安装前
缓存的指针，验证其在安装后也能清除 PresentEnd/OOB 边界。安装前不发布 PCL lease，
游戏仍执行原函数；这段有限的观察空窗不被伪装成已安装。

### 模块身份与第二个 callable

`GetModuleHandleExW(FROM_ADDRESS | PIN)` 在登记前执行，固定的是实际返回函数的
模块，不是仅固定同名 interposer。Windows 的
[PIN 合同](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw)
使该模块在进程结束前保持加载。失败时不保留待装地址；以后真实查询可以重试。
已登记地址不会在 Poll 前卸载并被复用。已有 pinned provider 也不再按文件名重新
查询，避免 marker 重试把既有钩子和源帧 epoch 归给另一个同名 HMODULE。

两个不同 callable 不能共享一个 trampoline。保留首次固定的 owner；出现第二个
不同目标时，将 PCL 身份标记为歧义。这个标记也覆盖多个首次查询并发竞争的情形。
它在本进程保持有效，不猜测新插件是否等同于旧插件，不迁移旧钩子。

真实 `EvaluateScope` 改为通过 `CurrentStreamlinePresentSource` 读取 PCL lease。
第二个目标即使出现在另一线程，旧线程的 TLS 也不再能授权后续 FG 输入。
明确资源 lineage、`slEvaluateFeature` 的当前 scope、原生 FG frame 合同仍按原顺序
参与选择。FG options 观察、SDK 设置、源帧 epoch、NR 算法及资源/fence 退休均未替换。
`FrameValue`、`ViewValue`、`Remember` 的三处同一行语句拆开，消除现有 GCC
misleading-indentation 警告，让完整适配器 fixture 可使用 `-Werror`。

## 3. 可重复验证

```sh
python3 tests/test_streamline_factory_lock.py -v
ASAN_OPTIONS=detect_leaks=0 NR_STREAMLINE_SANITIZE=1 python3 tests/test_streamline_factory_lock.py StreamlineFactoryTests.test_production_lock_and_lifetime_contracts -v
```

第一条编译完整生产适配器，并从 `nr_source_frame_runtime.inl` 提取**实际 Feature11
源帧选择链**一起编译。只有 Win32、MinHook、生产者/资源底层接口使用显式替身。
编译为 C++17，启用 `-Wall -Wextra -Wpedantic -Werror`。

| 场景 | 断言数 | 关键结果 |
|---|---:|---|
| 已持安装锁时进入工厂 | 4 | 回调登记目标并返回，不等待安装锁 |
| 两线程 loader/安装锁交错 | 6 | 回调先完成，Poll 再安装四个导出和 marker |
| 原生调用与实际 FG consumer | 38 | 返回值/参数不变；旧缓存地址的 end、F6、OOB、跨线程 owner 歧义都覆盖 |
| 目标模块 pin 失败、observer pin 失败 | 6 + 6 | 无悬空登记；允许后续查询重试 |
| 卸载与登记边界 | 4 | 不登记已失去寿命证明的地址；PIN 后拒绝卸载/复用 |
| 12 线程查询相同、不同目标 | 29 + 29 | 原指针原样返回，只安装一个 owner；异目标否定 PCL 身份 |
| 已有导出含外来 hook、owned enable 失败 | 12 + 12 | 不接管外来 trampoline，保留原对象重试；marker 不被 ready/backoff 丢弃 |
| deferred marker 外来 hook、owned enable 失败 | 4 + 4 | 不安装失败对象，不因重复查询绕过重试节奏 |

合计 **12 个场景、154 条断言**。ASan + UBSan 对同一组生产场景通过。
LeakSanitizer 明确关闭：该环境之前不能读取所需 `/proc` 进程信息；不报告泄漏检查通过。

负控共 **11 个可编译变体、12 次失败执行**，分别恢复旧阻塞回调、取消两端之一的
PIN、漏掉 deferred marker、恢复过早 ready 返回、取消首次提前 Poll、漏掉并发
owner 歧义、允许 getter 借用旧 TLS、恢复消费者直接读取 TLS、漏掉 end 失效、
重新按同名模块切换 provider。所有变体都在对应合同断言失败，不能靠编译失败算有效。

额外回归：`test_rr_depth_projection.py` 通过；`test_rr_current_projection.py`
在 Linux 明确跳过 Windows 合同，不计为通过。原有 Windows
`nr_fg_streamline_test.cpp` 和整包 canonical build 仍需 Windows 执行。

## 4. 证据身份与剩余验证

| 生产文件 | SHA-256 |
|---|---|
| `src/nr_fg_streamline.inl` | `a0ffa716b351d59c464a47e0d77814aaf41721866989da84543302d38f8a87b9` |
| `src/nr_source_frame_runtime.inl` | `e899ea0a130e0b768c7da46d9e898c5d53df51b32f575c697d51ef24576bf5f8` |

后续在 Windows 运行现有适配器测试、规范构建及真实 MinHook/loader 组合；游戏中
检查单次 factory 查询、冷启动、F6、FG 开关、插件切换和退出。PCL owner 歧义是
保守身份拒绝；若这导致持续旁路，需结合 F8、实际后端日志及精确源码/产物收据定位。
本次不宣称多 API 接入、注入式 FG、未知插件热切换或游戏稳定性已经验收。

没有复制 OptiScaler 的 loader-lock 实现或引入其运行时；第三方许可证边界不变。
