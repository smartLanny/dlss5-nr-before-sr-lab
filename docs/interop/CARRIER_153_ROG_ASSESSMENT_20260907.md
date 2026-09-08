# #153：ROG carrier 对照、关闭开关修正与待决策边界

本次从 `main@db0577c565d0a81a6f5c010b35698e078adf1205` 接续，不重做 #155 文件名识别。**尚未定位 BG3 / 法环 / 沉没之城2 / 2077 的实际崩溃指令**。已确认一个可独立复现、可小修的问题：`source=off` 在私有设备 bootstrap 之后才被检查。其余自动 API 排除与跨 API 生命周期改造没有实施。

## 证据身份与新旧差异

| 对照 | 精确来源 | carrier SHA256 |
|---|---|---|
| r3-exp2 实物 | [Actions run33988780219](https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/33988780219)，manifest source `0f714b3`、carrier upstream `29baa4c`；514560 bytes，PE1.4.11.0 | `f6f21d14487bd867ec145213db02b537820d9dc3602701469ccaf43c5059b270` |
| 发布 multiapi-dev3 | manifest source `b03f474ba5127ab057fbb5e93708c47eb9dbb866`、carrier upstream `28aed4099b0fe1c207b20b5fee5364c0773c25c2`；809984 bytes，PE1.4.12.0 | `70aaad240c9b3b5284d02048ca9ad816b55909021da8f6f4a23d6b8084f4bfc4` |
| 当前 carrier 生成代码 | `dccb5b3` dx11-compat 与上述 dev3 的 carrier 源文件哈希相同；#154 构建也没有改 carrier 源码 | 不把文件名/显示版本当源码差异 |

直接读取了 #153 Ezekiel 的 offline ZIP。其 carrier 哈希与上表 dev3 完全匹配；core 日志最后记录 Feature18 create-time tuning，不能证明调用已返回或异常发生在哪个模块。ZIP 内没有 carrier 自身日志、异常栈或转储。NeroEggs 所述 Vulkan 启动日志记录 `bg3.exe` 和 D3D12CreateDevice / DXGI runtime；这证明出现过 D3D12 设备，不能仅凭这个辅助路径断言主渲染 API 或故障线程。原始反馈仍留本地，没有上传复制品。

| 检查点 | 实际结论 | 对故障的解释边界 |
|---|---|---|
| DllMain 的 loader 选择、异常 filter、NGX 通知/Hook | 新旧均有；HookNewNgxModules 的核心选取逻辑没有增量 | 缺 API 准入是继承问题，不能独自解释“换新版才崩” |
| NGX Init / Create / Evaluate 与 D3D12 device 的 SEH | 新 completion patch 移除了旧捕获后返回 marker/E_FAIL 的转换 | 真实新增差异；某个原先被捕获的硬件异常可能变成退出，但没有游戏异常栈证明这个假设 |
| carrier 编译器 | 旧 r3 的固定 workflow 用 MSVC `cl /O2 /MT /EHsc`；新完整构建器用 LLVM-MinGW clang++23.1 `-O2 -fms-extensions -shared -static` | 新旧实物并非只差源代码。NGX参数的17槽ABI已有显式适配/门禁，但CRT、异常展开或生成代码仍是未排除变量；没有证据证明编译器导致崩溃 |
| 提交/退役 | 新版等待真实 progress fence，500ms 帧完成窗口；超时 quarantine 保留依赖；旧版有 CPU Signal 和超时后释放 | 可影响停顿、性能、失败处置；不能为消除卡顿恢复假完成或提前释放 |
| DX11 合同 | 新版 shape=1、独立 subrect、Create flags 与 native-history | 影响镜像合同；尚无证据把初始崩溃锁定于其中一项 |
| device/session | ForwardEvaluate 的 `g_probe_done` 与单例 `g_bridge` 只初始化一次；ctx4、shared fence、镜像资源属于首次 device | 没有按新 ctx/device 身份完整重建的合同；属于重建/多设备风险，未在本轮游戏中复现 |

## 最小 Windows 实验

使用真正的上述 DLL，宿主仅导出 ReShade 注册替身和一个具有 NGX 名称的测试 DLL。没有安装游戏、调用 NVIDIA 模型或假造游戏通过。

1. **旧 r3-exp2 与已发布 dev3 的加载对照**：没有 device/present 时，两版均 `carrier_loaded=1`、D3D11 `exports_patched=1`；卸载后 Hook 字节恢复。这把“无需要时仍有启动副作用”从源码推测提升为实物结果，同时排除了它是新版独有逻辑的说法。
2. **异常 filter 反例**：当前完整 MinGW DLL 在原 filter 为 null、原 filter 为宿主函数、ReShade 拒绝注册三个场景均恢复正确。仅看到 `if (g_prev_filter != nullptr)` 不能跳过 CRT 层而宣称悬空；本轮没有为这个未复现疑点加补丁。并发卸载仍不在该实验范围。
3. **关闭开关**：宿主创建真实 D3D11 WARP context，参数和 NGX 返回用替身；没有 consumer、没有 present。确认 carrier 实际配置回显 `source=off` 后调用一次 Evaluate。旧逻辑仍记录 `opening D3D12 session` 和 `D3D12 device created`；NGX D3D12 exports 缺失后才失败返回。修正后的同样实验：原生替身仍返回1，私有 device 创建次数从1变0。

用于实验的 `build/loader_probe.cpp`、`build/ngx_stub.cpp`、原始 stdout 与 carrier 日志保留在本轮隔离目录的忽略路径。它们是本地测试宿主，不是新产品诊断系统，也没有把临时日志语句加入生产源码。实际生产回归继续使用仓库已有 `tests/native_bridge/forward.cpp`。

## 已提交的小修与验证

源码提交 **`66b30eae315aea7b342a0a91f05b64863714a9a6`**：仅改变 `src/interop/nr_native_bridge_forward.inl` 的读取顺序与关闭准入。在现有 bridge 锁内先执行原有、限频的 CfgReload；source=off 不查询 consumer、不启动私有 device、不消费首次启动 latch。原生 Evaluate 仍按既有路径恰好一次转发并保留返回值；打开后仍可首次初始化。没有移除 NGX Hook、吞硬件异常、修改 fence/资源生命周期或添加游戏名名单。

- 现有生产 forward 回归先以 `init_calls == 1 && g_probe_done == 0` 失败；修复后通过，覆盖初始关闭、原生失败返回、关闭期间无 consumer 查询、再开启、原先 Reset/原生次序/partial-output 回归。
- `python -m unittest discover -s tests/native_bridge -p test_prepare.py -v`：6项通过。
- 使用 [固定 Windows 完整构建入口](VALIDATE_DEV3_WINDOWS.md)，对 **66b30ea** 完成 staging、35项构建检查、生产 shader、命令状态/WARP 正负控、48 scaled-work WARP、carrier completion 与真实异常子进程、双语 core/nrchain/carrier 编译链接和静态分析。具体清单见本地 validation；保留既有编译 warning。
- 实际修正 carrier SHA256：`cf451d842754cca23025b598154efaaa90a843911c92f6a4f8abc48e219ca196`。
- `build/source-off-candidate/validation.json` SHA256：`0d98243446a8c44de2f01032294366ac326d8392a9ff2f5399fbc79f226ede31`；`build/source-off-build.txt` SHA256：`989d0915337cd5f0628e1664b5de54312d52792e6e0700441fbdaa0546874a12`。
- **Windows compile / WARP 通过；NVIDIA Feature18 / game runtime 未执行。** 此独立构建不含 #154/#149/#150/#151/#155/#156，不能把其他候选验证混入本源码。

source=off 修正后，DLL 仍被映射，现有 startup Hook 仍存在；不能称“按 API 自动休眠”完成，也不将手工 source=off 当全部 carrier 崩溃的解决方案。

## 需要维护者决定的后续范围

建议采用分阶段方案，先做按实际呈现链激活，同时保留下一轮同条件旧/新/无 carrier 对照；不直接重做跨 API 资源架构。

| 方案 | 实现范围 | 风险 / 推荐 |
|---|---|---|
| A：runtime 按实际 DX11 呈现链激活 + Manager 按 API 默认不装无关 carrier | DllMain 仅轻量注册；loader/probe/Hook 有副作用的部分移到确认需要 DX11 bridge 之后。安装器控制文件是否存在，runtime 兜底误拷贝 | **推荐下一阶段**。必须解决早于呈现的 NGX Create、辅助 D3D11 device、多 ReShade、晚载入与卸载中回调。仅判进程中有 d3d11.dll 或任意D3D11设备不够。两端都做，不能互相代替 |
| B：先保留激活结构，只定位新版实际 fault | 固定相同 core/INI/runtime/游戏场景，对旧carrier、新carrier、无carrier采一个 crash stack或hang dump，再据实际fault决定局部修复 | 改动风险最低，可与A并行；单独B不满足“无关游戏自动排除” |
| C：现在重做多 device/session/shared-resource 与异常隔离 | 按 device/NGX handle 独立持有状态、回调退役、跨 API 资源所有权与完成协议 | 面积大、需要 DX11硬件/重建/退出系统回归；本轮**未实施，不建议与A一次打包** |

不能直接改成“等第一次 Present 再安装 Hook”：可能漏掉此前的 NGX Create/flags，造成 DX11首次启用假阴性。也不能在任意 D3D11 init_device 就启动，因为辅助设备并不代表游戏呈现链。单例 device 退役与异常隔离若必须重做，应单独列入C并重新确认范围。

loader callback 当前仍做枚举/版本读取/日志/Hook，和 Microsoft 对 [LdrDllNotification](https://learn.microsoft.com/en-us/windows/win32/devnotes/ldrdllnotification) 回调不得调用其他模块函数的限制不一致；[DllMain 最佳实践](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices) 也要求尽量推迟初始化。这是可确认的设计边界，不是本轮游戏 crash 已证实的栈因果。

编译器身份来自 [r3 source0f714b3 的固定workflow](https://github.com/smartLanny/dlss5-nr-before-sr-lab/blob/0f714b32597e395d0684567ed9296dc1d2a1f35c/.github/workflows/native-bridge-r3.yml) 的“MSVC carrier”步骤与本轮 validation/build脚本；旧包的core使用LLVM，不能据此误认旧carrier也用LLVM。本机未找到VS C++工具链（vswhere与cl均不可用），因此本轮没有做同一新carrier源码的MSVC/LLVM实物A/B，也未安装全局工具链。后续B应保留这个独立变量，优先用同源双工具链和同场景异常栈区分，不能只回退SEH便宣布根因。

#153 保持 open。#155 只验配套身份；#154 有限 blend、#149/#150现有诊断、#156 loader、#151 LightingLock 独立收口，不以本研究代验。
