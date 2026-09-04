# 装机宅 DLSS 5 管理器 + Quickbar · 0.2 实验版

单页本地管理器 + ReShade 原生半透明调节面板。无账号、无广告、无独立注入器。

**这是未完成游戏实机验收的实验版，不是可直接公开发群的完整安装包。** Quickbar 已通过 GitHub Windows MSVC 编译、链接和 CTest，并产出 x64 Add-on；桌面测试已在 Windows/Linux 通过。配套核心 wrapper、Electron 安装包、NVML 实机读取、游戏输入/HDR/插帧兼容仍待验证。证据与构建编号见 `docs/CI_RESULTS.md`。

## 用户操作

桌面上选择游戏，安装经校验的配套组件。卡片原地展开，配置不跳第二个详情页。

游戏内默认 **F10** 打开 Quickbar，**F6** 保持 AI 总开关，**F8** 保持诊断抓帧。
**Home** 或面板的“原始 ReShade / 高级”仍能进入原界面。F10 正常使用不打开 ReShade 主窗口，但不会删除宿主、版权信息或错误提示。

Quickbar 左侧调 AI，右侧展示观测 FPS、帧率/帧时间曲线、GPU 占用、功耗、温度和设备总体显存。可固定只读性能条。半透明色块和细边框构成玻璃观感，**没有真实背景模糊 pass**。

## 架构和版本约束

- 本安装路线只支持 **Windows、x64、原生 D3D12 + 普通 DLSS SR**。不能因为 DX11 也使用 dxgi.dll 就把它列为受支持。
- 0.4.1 的 AI 强度绑定 `TransferStrength` 和 `PostTransferStrength`，不是旧的 `Intensity`。配置版本不明时只读拒改。
- 游戏内修改走 `NRUI_GetStateV1` / `NRUI_SetValueV1` C ABI，保留核心 F8 锁、generation 检查和 ApplyConfig。旧核心没有接口时设置区禁用，Home 回退仍可用。
- 桌面修改 INI 必须先关闭游戏，使用文件 revision 检查和原子写入。删除了 v0.1“多入口同时写 INI 保证半秒生效”的承诺。
- ReShade v6.8.0 / API20 / ImGui1.92.5 是面板构建基线。不是任何 ReShade DLL 都兼容。
- NVML 只从系统目录加载，采样在线程里约1Hz，不支持/过期显示 `--`。多显卡必须选择设备，不能当成游戏专属占用。
- 观测 FPS 来自 ReShade 回调间隔，**不等于插帧后的显示 FPS**；不冒充 PresentMon 的显示帧统计。

## 启动和预览

```powershell
git submodule update --init --recursive
npm install
npm start
```

离线预览：打开 `preview/index.html`；也可 `npm run preview` 后访问控制台给出的本地地址。预览明确使用样例数据，不操作电脑或游戏。

## 两份源码配套

本工程包含 `native/`（独立 Quickbar Add-on）和 `core-integration/`（供已审查 0.4.1 核心使用的适配文件）。在核心实验分支中：

- 将 `core-integration/nrui_api.h`、`nrui_exports.inl`、`nr_before_sr_quickbar.cpp` 放到核心 `src/`。
- 将 `core-integration/build-quickbar.ps1` 放到核心 `scripts/`，运行该脚本构建带控制接口的核心。
- 原 `nr_before_sr.cpp` 不修改。脚本校验该文件的 Git blob，防止静默适配未来不同内核。
- 单独按 `native/README.md` 用 MSVC x64 构建 `zjz-quickbar.addon64`。不要用 MinGW 编译这个 ReShade/ImGui 面板。

准备组件包时显式指定文件，不再猜测含中文/版本号的 Add-on 名称：

```powershell
./scripts/prepare-payload.ps1 -AddonFile <核心addon路径> -BridgeFile <bridge路径> -ConfigFile <ini路径> -NeuralRuntime <NR运行库路径> -ReShadeAddonRuntime <ReShade64路径> -QuickbarFile <Quickbar路径>
npm run build:portable
```

NVIDIA、ReShade 和其他二进制不随源码提供；发布前需确认来源、许可和配套版本。`bundle.json` 的 SHA256 是一致性检查，不是发布者签名。

## 安全恢复

安装必须关闭游戏；未知代理 DLL、其他 NR 核心、反作弊目标会被拒绝。只管理自己记录的文件；升级/卸载遇到外部更改先停止。真实写前日志支持自动失败回滚和显式中断恢复，不递归删除游戏备份。

**v0.1 收据不可直接迁移**：请先用旧管理器卸载旧版本，再安装0.2。0.2拒绝旧或损坏收据，不猜测文件所有权。

## 开发

`npm test` 不需要安装 Electron。Windows/C++ 构建和待测矩阵见 `docs/VALIDATION.md`。当前指定 Electron44.2.0，仍需在联网构建机生成 lockfile 并完成发行包回归；此环境未安装其依赖。

扫描器等基础来自 DLSS5-Swapper / Rakan Alkhaldi (MIT)，固定 submodule。作者/主页/群号统一在 `product.json`。**野生的装机宅**是维护者署名，不是登录账号。
