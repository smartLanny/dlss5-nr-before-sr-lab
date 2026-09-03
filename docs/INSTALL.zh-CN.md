# beta0.3.2 延迟模块兼容候选版快速安装

插件名称：**DLSS 5 AI渲染超分版 beta0.3.2-dev**

作者：**@野生的装机宅 Bilibili**

唯一官方发布地址：[野生的装机宅 Bilibili 主页](https://space.bilibili.com/941799)。其他来源即使文件名相同，也请先按包内签名清单验证。

## 适用范围

- 64 位 D3D12 游戏；
- 游戏本身必须调用原生 DLSS Super Resolution（NGX Feature 1）；
- 已安装允许加载 `.addon64` 的 ReShade / RHI；
- 仅限离线、无反作弊的环境。

DX11、Vulkan、没有原生 DLSS SR，或被 OptiScaler 等转换层完全接管的
游戏不属于此插件的直接适配范围。

## 安装

1. 完全退出游戏。
2. 打开 ReShade/RHI 当前实际使用的 **Addon 搜索目录**。如果没有单独
   配置，一般就是游戏真正的 64 位可执行文件和 ReShade DLL 所在目录。
3. 删除旧版 `nr-before-sr*.addon64`、旧语言版以及会重复接管 NR 的另一
   个 DLSS5/RenoDX/ShortFuse addon；不要同时启用两个 NR 所有者。
4. 把压缩包中的下列文件放在 **同一个目录**，不要把 DLL 与 addon
   分开：
   - `DLSS5-AI渲染超分版-beta0.3.2-dev-@野生的装机宅-Bilibili.addon64`
   - `nrchain_nvngx.dll`
   - `nr_before_sr.ini`
   - RTX 50 包内的 `nvngx_dlssnr.dll`
5. 启动游戏，在 ReShade 的“插件”页确认界面顶部显示 beta0.3.2-dev、作者名和官方地址。

插件会从自身目录读取配置、桥接 DLL 和 NR runtime。因此，仅让 ReShade
看见 `.addon64`，但把其余文件放到另一个目录，会出现界面能打开、所有
功能却不生效的情况。

## 快速使用

- `F6`：总开关。关闭时应为纯游戏原生 SR，Feature 18 调用应为 0。
- 默认开启“使用渲染分辨率 NR → SR”：按游戏当前 DLSS 挡位的真实渲染
  分辨率先运行 NR，再交给游戏原生 SR。
- 关闭该链路开关：切换为 `SR → NR`，用于全输出分辨率高画质/性能对比。
- RR/光线重构活跃时，正式版会显示“RR 安全旁路”，不会叠加第二次 NR。

## 报错时请提供

请提供插件界面里的版本、当前状态/旁路原因，以及同目录下
`nr-before-sr.log` 中匹配问题前后的最小片段。beta0.3.2 会记录状态机、
延迟模块观察、颜色阶段归因、前几次诊断和稀疏性能摘要，不会逐帧刷屏。分享前请删除用户名、
绝对路径、账号、进程地址和运行库文件。

若 HDR 画面整体偏亮或偏暗，请在同一固定机位依次记录 F6 关闭、默认
`NR -> SR`、以及开启“实验性光照匹配”的 `NR -> SR`，每组停留约 15 秒。
不要同时改风格、强度或曝光；这样日志才能区分代理合同、NR 模型偏差和
最终合成偏差。

RTX 40 社区测试包不包含 ShortFuse runtime。请只从合法来源自行提供与
测试说明匹配的 runtime，并继续把 addon、bridge、配置和 runtime 放在
同一目录。RTX 40 尚不能称为全系兼容。

## 验证官方文件

在解压目录打开 PowerShell，运行：

```powershell
.\Verify-ReleaseManifest.ps1
```

看到 `VALID` 才表示包内文件与“野生的装机宅”发布密钥签署的清单完全一致。该签名清单用于确认来源和防篡改；插件 DLL 本身暂未使用商业 Authenticode 证书。
