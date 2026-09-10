# DX9 provider R2 控制与深度来源

此 provider 由本项目维护，调用 Windows 系统 D3D9On12；x86／x64 游戏均由独立 x64 宿主中的唯一 NR Core 处理。它传递同帧颜色和深度，运动向量由 VORT 估计，仍属于 Synthetic 后处理路线。

## 本帧深度

原实现仅在 Present 时调用 `GetDepthStencilSurface`。真实 Batman 诊断已观察到游戏在场景绘制后解绑深度，Present 返回 `D3DERR_NOTFOUND`；旧实现因此提前返回，未建立处理资源。这是旧实现的根因证据，不能用来宣称 R2 已完成真实游戏验收。

R2 监听 ReShade 的深度绑定和绘制事件，读取该设备实际绑定的 D3D9 深度表面。只接受带深度用途、与当前输出尺寸相同且无 MSAA 的表面，每帧最多保留 16 个候选；按本帧绘制的顶点／索引数量排序，数量相同再比较 draw 次数。Present 取出选定表面后立即清空候选容器；当前仍绑定的深度可作为回退。未找到深度时保留游戏原帧。

候选引用不会跨 Present、关闭 NR 或 swapchain 销毁／Reset 保留。受控 Reset 测试刻意在绘制后、尚未 Present 时触发重置，确认待用深度引用能被释放。此规则是受控来源选择，不等于证明任意游戏的最终场景深度都已正确归属；真实画面仍需核对。

日志 `[nr-feeder-dx9-depth]` 给出 `current-frame-scene` 或 `present-bound`、Present 时原查询结果、绘制计数和尺寸。实际 NR 成功仍须同时取得当前会话的 `[nr-feeder-client-completion] output_ready=1 nr_completed=1`，单独深度记录或宿主连接不能证明 NR 完成。

## 独立 NR 总开关

ReShade 面板“装机宅 DLSS 5 · DX9 回填”中的“启用 NR 回填”读写 provider 目录下 `dlss5-feed.cfg` 的 `enabled=0/1`。其他 Feeder 选项原样保留。保存使用新临时文件、刷新和原子替换；无法读取或解析配置时关闭 NR 并显示原因。外部文件更改每 250 毫秒读取一次，面板成功保存立即更新本进程状态。

关闭时 Present 提前返回，不创建新处理会话、不提交宿主帧、不复制 NR 输出，并释放待用深度候选；已有宿主可保持空闲。重新开启强制新的共享资源与历史，之后才接收和回填新帧。此开关独立于 ReShade 的全局着色器开关。Core 参数仍在管理器调整，退出游戏后应用。

## 只读状态接口

`NrFeederDx9QueryStatus(NrFeederDx9Status *output, uint32_t size)` 使用 C 导出及 `__cdecl`，大小必须等于 56 字节。结构由 `scripts/feeder-beta3/nr_feeder_query.h` 定义：两个 32 位字段 `size`、`enabled`，随后是六个 64 位进程累计计数 `presents`、`submitted`、`copied`、`nr_completed`、`disabled_presents`、`scene_depth_frames`。查询只复制计数，不调用 Core 或修改渲染状态。

`submitted` 计上传完成后尝试向宿主提交的帧；`copied` 计宿主输出复制完成的帧；`nr_completed` 进一步要求宿主确认 NR 完成。它们可以不同，不能把 `copied` 当作 NR 完成。计数接口服务于受控验收；实际游戏状态还需绑定进程、启动时间、组件摘要与新日志游标。

## 固定验收

R2 主源 SHA-256 为 `6e642c29e7c4de422e5479354b273b11f0603d44783e6054657d3060de7d8a88`。x86 provider 为 `f9b8d9b32411cf7118e0f00f66af7528db51a2742e815c797a0e00b8878dd221`；x64 为 `cb89471ab6c3a378be140d83d195d69de9e5238a37af1e88e704f72a56b2d731`。

两份二进制均通过已归档的 Defender 检查。两架构各完成 600 帧解绑深度、NR 关闭／开启、ReShade 全局效果独立性、带待用深度引用的 Reset 和 OS 正常退出验证。机器记录见 `resources/legacy-runtime/acceptance/dx9-r2-build.json`，原始源码、编译命令、日志与旧资源恢复身份归档于 `build/feeder-d3d9on12-r2-promotion`。RTX40 硬件与真实游戏验收保持未验证。
