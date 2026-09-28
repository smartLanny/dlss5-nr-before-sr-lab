# #317 / #298 恢复执行回执 — 2026-09-28

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
