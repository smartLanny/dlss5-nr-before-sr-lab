# Manager 0.4.7-beta.1 候选交接

本分支基于 dev.13 / `29ccaeb`，沿临时启动排障提交 `631a8aaf` 继续实施。完整解压版、Setup 和 portable 使用同一份生产源码与固定运行配套；不合入 main，不公开发布。原维护目录和用户现用安装未被替换。

## 行为变化

- GUI 默认 `asInvoker`，portable 外壳 `user`，Chromium 沙箱保持开启。管理员重启只响应用户明确操作，以随机 nonce、父/子会话和窗口显示、页面加载、renderer-ready 三项确认交接；取消或失败保留原窗口。游戏通过独立助手使用同用户普通权限令牌启动。
- 一键准备按资格分别处理 NR、SR、FG，保留已有配置及旧 SR 基线。新 SR 请求质量档与适用模型；RTX50 原生 FG 请求 2 倍；RTX40 新组件默认跟随游戏。配置请求不代表游戏已经开启对应功能。
- 准备意图在各子事务前持久保存。失败先恢复本轮新增 SR/FG 设置，再撤销新增组件与 NR；恢复失败保留记录并阻止继续写入或启动。底层文件/驱动日志和补帧迁移分别恢复，不嵌套文件事务。
- 新 RTX40 路线只安装 MFG Unlock 0.6.1，旧 v1 组件仍可识别、恢复和显式迁移。只写 `ReShade.ini` 的 `RenoDX.MFGUnlock/Enabled`、`ForceMultiplier`；不暴露旧动态目标、MaxCount、帧节奏补丁或全局策略。更高倍率只向上请求，不能作为关闭/降低 FG 的开关。菜单为 ReShade → Add-ons → MFG Unlock。
- Feeder 为独立 x64 DX12 / RTX50 / RGBA8 明确 sRGB 成品帧 NR 路线。使用项目 Core 唯一 NR owner 与估算导引；不把配套私有 DLL 认作游戏原生 SR/FG。原生 DLSS、Feeder、Vulkan 各有固定配套和恢复边界。
- 主反馈入口根据有效 Feeder 收据授权所选 EXE 旁固定 Add-ons 日志目录，保留 `nr-before-sr.log` 与 `dlss5-feed.log` 的有界头尾内容；任意目录、其他 EXE、伪造收据及链接不获授权。正式复核修正后重新打包，早期 `dist-beta1-final`、`dist-beta1-verified` 和 pre-review ZIP 仅为候选证据，最终目录为 `dist-beta1-release`。
- 正式复核补齐 REFramework、Feeder 和 FG 的专用恢复入口。一键准备记录与通用写入限制不会挡住恢复入口；未知/混合 API 游戏可显式选 DX12 进入符合条件的 Feeder 路线。FG 在第一次文件修改前绑定事务归属和游戏 EXE，并保存目标前后摘要；即使复制后尚无组件收据，中断也必须经过专用恢复。外部文件变更时保留文件和记录，不报告已撤销，不继续启动。

## 固定运行配套

- Core：`0.4.7beta`，SHA256 `93011d9283615ea9dc8e92955f5ca6aeff01435925f63e941dc1eea1128a372c`。
- DX12 Feeder：SHA256 `05f6c6d720378c05ff49332498cf065682806e4742cef473f150e0b844c12aa7`。
- Feeder recipe：`nr-feeder-dx12-047-sdr-20260909`，指纹 `734eb01140021930234c0c7ce7bb7472abad73f177a5b8fc582a95e67a6da778`。历史候选指纹仅用于原记录恢复，不能作为新安装来源。
- MFG Unlock：`0.6.1`，SHA256 `334a750accb892a8422b88f2891d91344f9dfeea9473bcd248f5e43a80d06b8c`，上游源码 `a975e84712a6bfc84284f50c9621c349ee806734`。MIT 原文、README 和来源固定于资源清单；二进制不提交进源码仓库。

## 验证与证据边界

正式复核修正后的完整 CPU/单元运行 `build/release-unit.tap`：796 项，793 通过，3 项需要额外样本而跳过，0 失败，包含共用 journal 的原生 NR、Vulkan、REFramework、旧无 owner 记录及新 FG 中断恢复。新添加的嵌包校验工具另有 `build/release-embedded-tools.tap`。早期三项真实 Electron 界面测试共 186 项通过；修正后的 FG 界面 141 项、游戏卡片 32 项通过，普通权限、沙箱开启、软件渲染，mock IPC 与临时 userData。LS pending、资格缺失时专用恢复按钮仍可用。这些检查不等于真实驱动/游戏联测。

Feeder 主反馈修正后的安装/恢复、反馈、原生/Vulkan 和 REFramework 定向回归 39 项全部通过。

启动预检先核对正式 EXE/ASAR 的隔离目录参数，再通过本地 debugger 装入无动作游戏服务验证普通与显式兼容模式；另一次仅带隔离目录参数的正式 EXE 启动不带 debugger、不替换服务，实际只读发现游戏并到达真实可见窗口与 renderer-ready。最终包须重新执行静态文件校验、正式 EXE 两模式与无仪器普通启动，结果保存在发行验证目录。该证据来自当前机器，不能替代此前无法打开的 RTX5070Ti 目标电脑复测。

Feeder 的受控 GPU 验证为 RTX5090 Laptop、640×360 初始画面与一次 768×432 resize，真实 ReShade 回调、三层深度及移动平面。固定 600 帧和关闭 NR 的对照具有相同 input/depth hashes；NR 开时有真实 Evaluate，关时没有。r3 重编译后重新建立新尺寸资源并恢复 NR 写回，1200 帧、3600 draws，持有进程句柄捕获 OS exit 0、无超时。

r3 写回计数在 resize 前为 293，新资源代际为 55 → 895，不累计相加。像素差来自每个样本同帧 Present 前后对同一缓冲的读回；NR-off 仍有 ReShade 覆盖层差异，不将“有差异”单独当作 NR 成功。OS exit 0 不代表卸载过程无警告：日志保留 commandQueue/device 引用计数和 Core 未 unregister 的提示，本轮未宣称完整生命周期无警告，也未为消除提示改动 Core。

- 联合摘要：`build/feeder-dx12-evidence-r3.json`，SHA256 `3dbdb51972e0ade859982bb649aebac8f988955de6b6c8f15305411971e9539d`。
- 冻结证据：`build/feeder-dx12-resize-r3-evidence/`，16 个被列举文件加 `evidence-identity.json` 共 17 个文件。identity SHA256 `808c4c7936c5c73e4d29a5c96ae94c1b1dbff1e712423e3c09bf7017cb5649ec`。
- 宿主 SHA256：`8ad0d8e9528396a2aa8998e03bec8c91b3cf5402c5741eb819a5b6dab8490f49`。

## 仍需目标设备验收

`realGameVerified=false` 保持不变。Feeder 的真实游戏画质/长时间稳定性、RTX40 上 NR 单独/MFG 单独/组合运行，以及此前 Win11 26200 目标机的默认启动，均未被当前机器的受控测试替代。NSIS `System.dll` 故障与管理器子进程启动故障分别跟进，完整解压版提供不依赖 Setup 的打开方式。

用户说明见 `docs/manager-0.4.7-beta.1-use.txt`，启动与恢复细节见 `docs/STARTUP-RECOVERY.md`。
