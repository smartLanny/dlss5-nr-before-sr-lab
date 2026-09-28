# #317 / #298 恢复执行回执 — 2026-09-28

## 最新接续：PR #318 固定 pin 后规范构建通过

本次最新干净提交为 `65b97819d493baa184c2bd484ccad538301116e4`。首层 RR 深度绑定运行时仍为 `23c0cb27ff60fc2e6fd455f157abf4deb489643e`，CPU history 夹具修正仍为 `abbdc5b33f06a430b9e0850b8515cb0d68feb356`；新提交只更新两处当前源码固定 pin。核对时实时 `main` 为 `b2990b120337cdd8026f83a1bd369f3708b78fbe`，本轮初始任务 HEAD 为 `4065cbe9` 且工作树干净。此处 `main` 是内部 PR 构建基线，不是 Beta 7 或 Beta 8 候选身份。

**规范整包构建在干净 `65b97819` 上 exit=0 / passed=true，产出中英文内部测试 Core 与 ZIP；首层深度和完整在途入口也通过。没有部署或游戏、独立硬件 GPU、真实 NVIDIA NGX 推理验收。调试层缺失，验证仍未完成。PR 继续保持 draft，#317/#298 的游戏与显存问题继续开放。**

| 层级 | 本轮结果与边界 |
|---|---|
| 当前源码 pin | 两处旧 Git blob SHA-1 经源差异审核后更新为 `ac51dc61cb2ab3567836c00d7251e33d7384013f`；固定比较和后续语义断言保留 |
| 聚焦测试 | 正常系统 TEMP 的 r2：colour-edge 10 项中 9 通过、1 项生成前跳过；format 8、picture 5、quality 6、colour-source 3 全通过；五入口 exit=0 |
| 首层深度回归 | 干净 `65b97819`，真实 ReShade/WARP 宿主编译、运行均 exit=0，PASS |
| 完整在途回归 | 干净 `65b97819`，dual、BindingPacket、Region WARP、CPU history 四宿主 PASS |
| 规范构建 | `scripts/build-unified-candidate.ps1` 原入口 exit=0；canonical source/compile/WARP/full-face、unified8、reconstruction 均通过；内部中英文 Core/ZIP 已产出 |
| Snapshot | D3D12 WARP 正例 3 场景 1045 项检查通过，错像素负控按预期拒绝；readback 与 negative control verified=true；本机无 debug layer，`snapshot_debug_layer_verified=false` |
| 仍未验证 | 独立硬件 GPU、真实 NGX Feature18、游戏画质/帧时/显存、调试层；构建完成后尝试另存附加 closeout summary 时 D 盘空间不足，该附加文件没有有效内容；原始 `request.json`、`result.json`、`build.log`、全部构建回执和规范候选产物仍保留。为保全并腾出提交空间，仅将构建中间目录的英文 Core 重复文件复制到真实 C 目录 `C:/Users/PC/.devspace/pr318-build-preservation-65b97819/original-build-English-Core.addon64`，SHA256 `1d76709c6cd45565ae9f0a456375efc1e7c0b3c3cc6fb559e3526ed1841f4dd9`。原文件、保全副本及规范 `candidate/TEST-en/nr-before-sr.unified10-reconstruction1-ui1.en.addon64` 三份哈希一致后，仅移除构建中间目录的重复原件。该保全副本可用于回退；迁移记录为 `C:/Users/PC/.devspace/pr318-build-preservation-65b97819/relocation.json`。规范 Core、两语言 ZIP、日志与新旧回执均留在原路径。

随后 Git 建立 index.lock 时再次遇到 No space left on device；以相同复制后核对三份 SHA256 的方式，将本次构建中间目录的中文重复 Core 保存到同一 C 目录的 original-build-Chinese-Core.addon64（SHA256 cd4f372b8fe7882604a5f4b3faf7d9211f396a76f978362894128ac0442fdee8），迁移记录为 relocation-zh.json。仅移除已保全的 build 中文重复件并立即提交文档；TEST-zh-CN 正式产物保持原位。

CI run `36436466517` 为 skipped；没有部署 |

**下文“当前结论”至“交付与风险”是 `abbdc5b3` 阶段的原始历史回执，含当时“规范构建未通过”的结论与两次失败日志，原文保留供追溯；本节及文末“最新正式收尾”是当前结果。**

## 旧阶段历史回执（原文保留）

以下记录当时的未通过构建与后续待办；本轮最新结果见上文和文末。

## 当前结论

接续原 PR #318，没有重建重复修复或接管其他 Beta 8 工作树。首层 RR 不可变深度绑定的运行时补丁仍为 `23c0cb27ff60fc2e6fd455f157abf4deb489643e`；本轮新增测试提交 `abbdc5b33f06a430b9e0850b8515cb0d68feb356`。main 核对为 `b2990b120337cdd8026f83a1bd369f3708b78fbe`。最终 Beta 7 调查基准仍为 `80dcc99952b9cdd277d9780b292a2a169a5ff83f`，不是 main。

**完整 `validate-dual-inflight.py` 阻塞已解除并在干净提交上通过；规范整包构建仍未通过。没有新的可交付 Core，没有游戏/实际 NVIDIA 推理/调试层验证。PR 保持 draft，#317/#298 保持开放。**

原始 findings、首轮红绿回归和旧失败记录保留在 [首轮调查](317_298_BETA7_FINDINGS_20260928.md)。其中“history fixture 编译阻塞”是首轮历史状态，由下列新回执接续，不删除旧记录。

## 本轮修改与验证

本轮唯一新增代码修改为 `tests/d13_inflight_history_host.cpp.in`：补上生产提取片段已使用的 `diagnostic_warmup_reset`，并在夹具的绑定适配中明确区分真实历史重置和仅向模型发出的 Reset。不能只增加默认 false 的成员，否则原有间歇/连续/丢帧预热断言会失真。没有修改生产重置逻辑、NGX、显存回收、队列、模型、FG、尺寸或默认设置。

新增断言验证：连续的模型-only Reset 不应让辅助层永远停在预热。原有 32 帧间歇调度、连续调度和真正缺失父帧的断言保留。独立负控只在生成的测试翻译单元中，将成功计数的判断故意换回 `diagnostic_reset_requested`；编译成功，旧检查仍走完，在新增模型-only Reset 检查处按预期 exit=1。生产源码没有被该负控改写。

| 验证 | 结果及精确边界 |
|---|---|
| 首层深度新回归 | 在干净 `23c0cb27` 重新编译运行通过；原先 dirty-source 回执保留，未重标 |
| 完整在途入口 | 在干净 `abbdc5b3` 编译运行通过，包含原始 dual、BindingPacket、Region WARP 与 CPU history 四个宿主；不再只是单独绕行执行其中两个宿主 |
| CPU 历史调度 | 间歇成功16次、真实重置16次、最终采用0；连续32次只重置1次、预热2次、采用30次；漏父帧后重新预热；模型-only Reset 后成功计数推进到3并采用 |
| GPU 生命周期 | 真实 ReShade/WARP 队列阻塞、A/B/A 输入轮换、精确 fence 退休与恢复、另一队列拒绝、8个 packet 上限和第9个拒绝、Region X24 readback/缺导引降级全部通过 |
| 模型-only Reset 独立负控 | 编译 exit=0，运行按预期 exit=1；只证明新增 CPU 断言能识别错误重置语义 |
| 规范完整构建 | 两次真实尝试，均保留失败日志；第二次停在本 PR 漏更新的固定源码哈希，未进入完整 Core 编译/链接 |

WARP 宿主仍有明确边界：NGX 回调为 stub 或不存在，Execute/fence 发布和 Reset 账目由宿主适配；不是完整 Core hook、硬件显卡、游戏画质、帧时或最终显示验收。

## 规范构建的实际阻塞

按 `scripts/build-unified-candidate.ps1` 运行，使用已核对的干净依赖：carrier `28aed4099b0fe1c207b20b5fee5364c0773c25c2`、ReShade `4a50d1eddace85734871d91792ff214f13f66c01`、DirectXMath `28929ac6f3c1649a7641741b689ebecccb17aa8d`、FFX SDK `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`；完整 ONNX Runtime 人脸后端和显式 FFX 对齐缓存路径均提供，没有禁用后端。完整命令在下列每次目录的 `request.json`。

第一轮因本机 `D3D12GetDebugInterface` 不可用，在 snapshot 宿主的 required debug layer 前提停止，HRESULT `0x887a002d`。第二轮使用仓库已有 `-AllowMissingDebugLayer` 模式；Beta 7 既有候选回执的 `snapshot_debug_layer_verified` 也为 false。该模式仍运行真实 snapshot GPU 正例和错像素负控，不把未安装的调试层记成通过，不安装系统组件。

第二轮实际失败为 `tests/test_colour_edge.py::test_actual_base_identity`：

```text
actual src/nr_before_sr.cpp blob: ac51dc61cb2ab3567836c00d7251e33d7384013f
expected stale blob:            32bfacf33c88ede6436ecb6aa26003f739aa23c9
```

`prepare-multi-api-beta.py` 的对应 pin 已随首轮运行时补丁更新；这个独立颜色重建测试 pin 漏更新，属于本 PR 的接续工作，不应称为无关旧故障。已重新查看实际运行时 diff：主文件仅更改 `PrepareAfterSr` 首层绑定接线。固定哈希比较和生成后的语义检查均没有被删除或改成自动接受当前源码。

精确字符串搜索还找到 `tests/multi_api/test_format_patch.py:90` 的相同旧 hash；**尚未完成该断言上下文审查，不盲目批量替换**。补查这个文件及进一步一致性验证的工具请求被安全层拦截，本轮没有改请求形式、改工具或改路径绕过；这两处 pin 本轮均未修改。因此不能把整包构建写成通过，也没有绕过失败生成入口另行裸编译/打包。

接续时先审核两处固定基线的用途，再只更新确需跟随本补丁的精确值，重跑原生成和规范构建。不要改断言为动态 hash、删检查、降低图形安全保护或直接把 main 全文件覆盖到 Beta 8。

## 本轮原始反馈复核

再次读取本地原始 F8：A 的恢复态窗口 scope.epoch=2 保持不变，后置 RR attempts 和 NR recordings 均 +17；不能用它证明 ESC 过渡没有丢失。C 的 scope.epoch=491→509，NR recordings +13、准备/深度拒绝均 +5，仍应按跨作用域计数处理。Beta 7 实际代码的 `BeginPipelineStatsScopeLocked` 会在来源代次/模式变化时清零局部统计；FG-owned 门控保护晚期最终表面，不能只凭该消息推断内联 NR 全部停止。

#298 原始 F8 的 fullNR.scopeMatches=0/enabled=0 与实际双层 evaluateSuccess +22 再次核对一致；没有新增显存时间线、泄漏证据或节省量结论。不合 #277，不联系 guest，不并私有 SR。A/B 与显存/FG 的游戏验收仍独立开放。

## 私有证据位置与哈希

根目录：`D:/ChatGPT/DLSS5/evidence/`。不发布原始反馈或运行库。

| 相对路径 | SHA256 / 说明 |
|---|---|
| `issue317-primary-depth-head-23c0cb27-r1/validation.json` | `df70630bb3476fc4f187b95ae00dbdfed113826d01957d6599faf2bdc57d1987` |
| `issue317-complete-inflight-head-abbdc5b3-r1/validation.json` | `b189486cbd720283c97506e57a9abbc33bd22af05e4c6028f468e74722c5f15b` |
| `issue317-history-reset-negative-r1/validation.json` | 负控命令、生成来源 hash、期望/实际退出码；同目录保留编译与运行日志 |
| `issue317-unified-build-abbdc5b3-r1/build.log` | `245a4a12b39f942d2a2c46ddd804f1d62ea744c8ba4bfe92a5379eba36562e2f` |
| `issue317-unified-build-abbdc5b3-r2/build.log` | `1c1d53a1f65393c7f60fb847b92138053b0b7ab19c45d5e4f23eabc39eb70e71` |

两次构建目录均有 `request.json`、`result.json` 和原始日志；记录 source_dirty=false、exit_code=1。临时首轮夹具修改及 dirty 测试回执另保留，提交后重新运行的是上表干净提交回执。

## 交付与风险

没有发布/部署新 Core、改 OTA/Manager pin、改游戏配置、启动游戏或启用付费 CI。主线与别人的工作树没有修改。源级修复与 CPU/WARP 验证成立，不等于整包 compile verified、NGX runtime verified 或 game runtime verified。

即使后续游戏中准备拒绝减少，也不能直接声称性能/VRAM改善：原来被旁路的 NR 真正执行后，工作量可能增加。须分别验证 C 的实际闪烁，A 的 ESC 过渡，B 的 UI+模型画面，以及同配置的显存峰值/FG/resize/退出。持续旁路或新增闪烁、崩溃应回退并提交 F8/Core 日志及准确候选 hash；不以旁路后的高 FPS 作为通过。

## 最新正式收尾：固定 pin、干净回归与规范构建

### 固定 pin 的审核与聚焦验证

前次规范构建的 `tests/test_colour_edge.py::test_actual_base_identity` 因旧源码 blob `32bfacf33c88ede6436ecb6aa26003f739aa23c9` 停止；`tests/multi_api/test_format_patch.py::test_actual_core_input` 也保存相同旧值。审阅 `23c0cb27` 对 `src/nr_before_sr.cpp` 的实际修改，变更为 `PrepareAfterSr` 的首层 RR 深度绑定接线；颜色 shader、颜色契约及格式补丁语义没有随之更改。两处当前源码 pin 因而精确改为审核后的 Git blob `ac51dc61cb2ab3567836c00d7251e33d7384013f`。`test_colour_edge.py` 从本工作树的 `REPO/p` 读取当前 `src/nr_before_sr.cpp`；`test_format_patch.py` 从规范入口设置的 `MULTIAPI_CORE_SOURCE` 读取同一当前源文件。两者核对的是当前源码，不是历史 fixture。仍以固定哈希拦截意外漂移，未改为动态接受当前文件，也未删除生成后的语义断言；`scripts/prepare-multi-api-beta.py` 的独立 pin 此前已更新。

聚焦测试的采信目录为 `D:/ChatGPT/DLSS5/evidence/issue318-pin-audit-4065cbe-r2/`，使用正常系统 TEMP。最初沙箱临时目录的 PermissionError 以同一原命令正常提权后解决；改 TEMP 的 r1 不作为采信回执。r2 的 colour-edge 10 项为 9 通过、1 项因尚无完整 compatibility generated export 而跳过；format-patch 8/8、picture-integration 5/5、quality-panel 6/6、colour-source-gates 3/3，五入口 exit=0。随后规范构建日志确认 `test_actual_base_identity`、`test_actual_core_input` 与生成后的 `test_explicit_generated_export` 均为 ok。生成 source manifest 为 `candidate/source/source-manifest.json`，文件 SHA256 `cacebe2cdbd254a3ec0ecd2bd1c78003b8b543d02e8036cc11bfe71195dd14eb`；其中提交为 `65b97819`。生成 Core 主源 SHA256 `4bab35e3e3c3422055b8ce73a7e0b951ce4e75d8721a6f4f19682af2a1d3d6c4`，生成 Region SHA256 `fd8d22824377ba44200d8602bd9c742cd3cc7fea0a40fd5c1009987db96325b3`，均与 manifest 的文件记录相符。

### 两条原回归入口

工作目录为 `C:/Users/PC/.devspace/worktrees/dlss5-nr-before-sr-lab-product-94d1a56b`；运行前后 `git status --porcelain=v1` 均为空。按以下顺序执行，`PYTHONDONTWRITEBYTECODE=1` 仅避免在源码树生成 Python 缓存：

```powershell
$env:PYTHONDONTWRITEBYTECODE='1'
python scripts/validate-primary-rr-depth.py --compiler 'D:/agent-tools/llvm-mingw/llvm-mingw-20260826-ucrt-x86_64/bin/x86_64-w64-mingw32-clang++.exe' --reshade-dll 'D:/ChatGPT/DLSS5/evidence/cp2077-bisect-20260927-030701/initial/dxgi.dll' --output 'D:/ChatGPT/DLSS5/evidence/issue317-primary-depth-head-65b97819-r1'
python scripts/validate-dual-inflight.py --compiler 'D:/agent-tools/llvm-mingw/llvm-mingw-20260826-ucrt-x86_64/bin/x86_64-w64-mingw32-clang++.exe' --reshade-dll 'D:/ChatGPT/DLSS5/evidence/cp2077-bisect-20260927-030701/initial/dxgi.dll' --output 'D:/ChatGPT/DLSS5/evidence/issue317-complete-inflight-head-65b97819-r1'
```

| 私有回执或输入 | SHA256 / 结果 |
|---|---|
| `issue317-primary-depth-head-65b97819-r1/validation.json` | `61fa0088ea848dca8560d50f3e5b1afa171c9c9e1193ca001eec9afa5380c746`；`source_dirty=false`，编译/运行 exit=0 |
| 同目录 `primary_rr_depth.cpp` / `run.log` | `4fa183d22a6c2f2d590933a05977831048f7f14f62a83e014e99e07cd87fad92` / `68d27ed03e1e2d377584bf27e8c2e2a972a3cafdbce1c56d81200e0cc4d5f3e0` |
| `issue317-complete-inflight-head-65b97819-r1/validation.json` | `3323cbb577090b84f82daa9b7eb9803b1879dd6fe6ba6f63332d96c5bacce5e7`；`source_worktree_dirty=false`，四宿主 PASS |
| 同目录 `d13_inflight_history_host.log` | `f059f26de31d1dd0a1c8b30ea81fb05e4b8bd8b3df3224165cd20fbcbfc35d50`；model-only Reset 后 `since_reset=3 selected=1` |
| 编译器 / ReShade DLL | `bf58cf0e544a308660645aad1fe65ae9cbc44b0871e4010278f3d9551f86a690` / `0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7` |

首层宿主的真实 ReShade/WARP 队列深度 shader、A/B/A、不可变绑定、区域开关、拒绝及退休检查输出 PASS。完整在途宿主包含 WARP 生命周期、BindingPacket、Region 和 CPU history。CPU history 的 model-only Reset 行为为 `requested=1 warmup_reset=0 since_reset=3 selected=1`。这些宿主用 NGX stub 或不调用 NGX，Execute/fence 发布及 Reset 账目有宿主适配；不等同完整 Core hook、硬件 GPU、真实推理或游戏验收。

### 规范整包构建与产物

规范构建私有目录为 `D:/ChatGPT/DLSS5/evidence/issue317-unified-build-65b97819-r1/`。其 `request.json` 记录 `source_commit=65b97819d493baa184c2bd484ccad538301116e4`、`source_dirty=false`；在上述工作目录执行的完整原命令为：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\Users\PC\.devspace\worktrees\dlss5-nr-before-sr-lab-product-94d1a56b\scripts\build-unified-candidate.ps1' -UpstreamRoot 'D:/agent-tools/dlss5-carrier' -ToolchainRoot 'D:/agent-tools/llvm-mingw/llvm-mingw-20260826-ucrt-x86_64' -ReShadeSourceRoot 'D:/agent-tools/reshade-source' -DirectXMathRoot 'D:/agent-tools/DirectXMath' -OutputRoot 'D:\ChatGPT\DLSS5\evidence\issue317-unified-build-65b97819-r1\candidate' -FaceRuntimeRoot 'D:/agent-tools/face-resources/onnxruntime-win-x64-1.23.2' -NrchainReuseRoot 'D:/agent-tools/nrchain-verified-d15' -FfxSdkRoot 'D:/agent-tools/FidelityFX-SDK-v1.1.4' -FfxCacheRoot 'D:/agent-tools/evidence/unified6-amd-sdk-aligned-r1' -AllowMissingDebugLayer
```

`result.json` 记录 UTC 2026-09-28 14:30:35 至 15:38:04、`exit_code=0`、`passed=true`、`source_dirty=false`、`game_verified=false`、`deployed=false`。canonical source/compile/WARP/full-face、unified8 与 reconstruction 均通过。Snapshot 的 D3D12 WARP 正例 3 场景共 1045 项检查通过，错像素负控按预期拒绝，最终 `snapshot_gpu_readback_verified=true`、`snapshot_negative_control_verified=true`。重建 WARP 正例 exit=0，禁用保护负控按预期 exit=1，`manifest_verified=true`。`-AllowMissingDebugLayer` 是仓库已有模式，没有关闭 WARP 正反例；本机缺 debug layer，`snapshot_debug_layer_verified=false`，不能记作调试层通过。

| 构建回执或内部产物（相对本次目录） | SHA256 |
|---|---|
| `request.json` / `result.json` | `1a297734b07ce77872848e0f971066587dad209ed68eb321e8140079166f1b8e` / `7ffaf0b8950aeee21d744be7d2bf39ad89ef509b5f71c10b1a9ddbd1da03e95b` |
| `build.log` | `6746455ee5ea1aacd86d7a1a5044cb546b42f75db98f1b94513f167656f0c590` |
| `candidate/validation.json` / `candidate/UNIFIED10-RECONSTRUCTION1-UI1-CANDIDATE.json` | `897994e847337900d2608bc50f34c0a34feb430bb2f387abcbd1b8921a74f374` / `9fad4a3052dd15ebfff10259f4a23c067e4f24e6d6805546850590c5a502c8c4` |
| unified8 / reconstruction 回执 | `84ab53e552264ab825c9cc7706c18ccc7e95450edb33bdc3c4a101f1f1a2d9e9` / `8c2b6366677e49a146561aeeb8eba75a932c576ea3b0a9e2528861cb5b7b2c9f` |
| `candidate/TEST-en/nr-before-sr.unified10-reconstruction1-ui1.en.addon64` | `1d76709c6cd45565ae9f0a456375efc1e7c0b3c3cc6fb559e3526ed1841f4dd9` |
| `candidate/DLSS5-D21-unified10-reconstruction1-ui1-en-test.zip` | `8b6f48e41f809e7e7404578f329d1de1546305a29a88b983e7bbcddbab3341f4` |
| `candidate/TEST-zh-CN/nr-before-sr.unified10-reconstruction1-ui1.zh-CN.addon64` | `cd4f372b8fe7882604a5f4b3faf7d9211f396a76f978362894128ac0442fdee8` |
| `candidate/DLSS5-D21-unified10-reconstruction1-ui1-zh-CN-test.zip` | `cf088a5f70f956ceb55c4879fb8fa9e45eaee55fca6b5cdcbf992ec285d5fc8d` |
| 本次复用的 nrchain DLL | `1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2` |

这些 ZIP 与 Core 是本次内部 main 基线 PR 验证产物；旧文件名不赋予 Beta 7、Beta 8 或既有 UI1 发布包的身份，也不作为已部署候选。规范测试仍有具体可选跳过：生成前 `test_complete_generated_source_when_provided` 跳过，生成后同组 11 项通过；`test_final_export_uses_successor_once` 的条件测试跳过，生成器实际检查了该行为；`test_full_face_real_assets_bilingual_and_tamper_controls` 因未设置 `DLSS5_FACE_TEST_ASSETS` 跳过，完整 ONNX 人脸后端仍编译。跳过项不记为通过。

构建完成后尝试另存附加 closeout summary 时 D 盘空间不足，该附加文件没有有效内容；原始 `request.json`、`result.json`、`build.log`、全部构建回执和规范候选产物仍保留。为保全并腾出提交空间，仅将构建中间目录的英文 Core 重复文件复制到真实 C 目录 `C:/Users/PC/.devspace/pr318-build-preservation-65b97819/original-build-English-Core.addon64`，SHA256 `1d76709c6cd45565ae9f0a456375efc1e7c0b3c3cc6fb559e3526ed1841f4dd9`。原文件、保全副本及规范 `candidate/TEST-en/nr-before-sr.unified10-reconstruction1-ui1.en.addon64` 三份哈希一致后，仅移除构建中间目录的重复原件。该保全副本可用于回退；迁移记录为 `C:/Users/PC/.devspace/pr318-build-preservation-65b97819/relocation.json`。规范 Core、两语言 ZIP、日志与新旧回执均留在原路径。

随后 Git 建立 index.lock 时再次遇到 No space left on device；以相同复制后核对三份 SHA256 的方式，将本次构建中间目录的中文重复 Core 保存到同一 C 目录的 original-build-Chinese-Core.addon64（SHA256 cd4f372b8fe7882604a5f4b3faf7d9211f396a76f978362894128ac0442fdee8），迁移记录为 relocation-zh.json。仅移除已保全的 build 中文重复件并立即提交文档；TEST-zh-CN 正式产物保持原位。

CI run `36436466517` 为 skipped，未启用付费托管 CI。没有部署、启动游戏、改配置/OTA/Manager pin、联系 guest 或并入私有 SR。A/B/C 的游戏画面与切换问题，以及 #298 的同进程显存峰值、FG、resize、退出仍需独立实测；当前没有泄漏或节省量结论。即使后续准备拒绝减少，也要核对真实工作量，不能把旁路形成的 FPS 当作改善。

文档提交后检测到 D 盘重新有足够空间，两份构建中间重复件均已从 C 保全副本按原路径恢复并重新核对 SHA256，C 副本继续保留；恢复记录为 C:/Users/PC/.devspace/pr318-build-preservation-65b97819/restoration.json。本任务未清理其他目录，未将重新出现的磁盘空间计作本任务清理成果。当前规范输出目录的原中英文 build 副本也完整保留。
