# #293 / #298：首次准备与显存驻留联合修复

## 范围与源码身份

维护者确认 Perf 录制和现版首开/显存反馈是同一持续问题，本批复用旧证据，不要求重交才能开发。
分支 `codex/293-298-coldstart-residency-20260928` 从实时 main
`b2990b120337cdd8026f83a1bd369f3708b78fbe` 开出；不修改 #313 的 Beta 8 工作树或 #318 的 RR 深度修复。

以下新 NVIDIA 实验读取最终 Beta 7 构建源 `80dcc99952b9cdd277d9780b292a2a169a5ff83f`
的冻结生成 Core、source-manifest、shader 和模块收据。`--candidate-codec` 只替换该宿主的实际
`EnsureCodec` 函数，用于隔离 PSO 修正，**不是完整本分支 Core 已通过验收**。
本分支的正式整包验证另记收据，不能借用该实验或旧 Beta 7 的通过状态。

本批未安装游戏、未合 main、未发布 OTA、不修改 Manager 默认 pin。main 生成器保留的旧内部
版本标签不是新版玩家交付身份；不得直接将内部验证产物作为 Beta 7/8 或已发布 UI1 覆盖安装。
后续集成人员为正式候选统一分配新版本并完成游戏验收。

## 实测结论

### 首次准备不只有模型创建

5090D，人工初始化 Color=0 / Depth=1 / Motion=0；历史 flags=73、Intensity=1.2、AutoMask=false、
UICorrection=true；NR 2304×1296，深度 2228×1253。实际 NVIDIA Create/Release 与 fence 完成，
**没有 Evaluate，没有游戏画质/FPS/F6-to-display 结论**。

| 场景 | API/bootstrap CPU ms | codec/7 PSO CPU ms | 说明 |
|---|---:|---:|---|
| 首次基线进程 | 372.632 | 3127.709 | 驱动冷缓存状态未人为清除或固定 |
| 候选磁盘未命中 | 385.704 | 434.248 | 后续进程，驱动已经热；不能把上述 3127→434 归因于补丁 |
| 候选磁盘 7/7 命中 | 369.822 | 83.631 | 同冻结 shader，实际缓存读取成功 |
| 热驱动对照：原实现，新进程 | 407.033 | 418.019 | 后续独立交替对照，不删除驱动缓存 |
| 热驱动对照：候选命中，新进程 | 380.063 | 92.589 | 同 GPU/合同/单层；样本少，不是通用节省率 |

首个真实模型 Create wrapper 约 258ms；其后四个独立实例约 100–103ms。这里是 CPU 包装调用，
不是纯 GPU 推理时间。同进程重复 API/codec 约 0.001ms，符合热 F6 不再明显停顿的机制。

HLSL 字节码已经在正式构建中预编译，本批不重复实现它。新增的是驱动 PSO 的磁盘缓存。
首次没有有效缓存、shader/驱动更换、缓存禁用/不可写时仍有原始管线创建成本；API 初始化和
Feature18 创建成本也仍在。**这不是“所有冷进程首次开启零卡顿”的承诺。**

### 显存：已释放对象不等于库内分配已经收缩

同一进程依次保留 5 个独立 NR Create，逐个确认独立句柄与创建命令 GPU 完成。

| 状态 | DXGI 进程 local usage（字节） |
|---|---:|
| 五个模型保留 | 3,510,403,072 |
| 释放四个、保留一个 | 3,205,005,312 |
| 最后一个也释放 | 53,051,392 |
| 相同合同重新创建一个 | 746,618,880 |

这说明在本运行库/样本中，仅删除辅助层仍保留了显著分配高水位；全旧实例排空后重建目标单层
能恢复接近初始单层占用。**这是进程测量，不是已逆向确认的厂商池结构，也不是任意游戏的节省量。**
独立单层进程和重建后的单层再次 Release 仍有约 637–639 MiB 残留，故不声称 Release 总能归零。
GPU Evaluate 可能触发额外分配，本宿主未覆盖；16GB/8GB GPU、其他运行库和实际游戏仍需验收。

本次全旧实例最后 Release 约 133ms、重新建单层总准备约 161ms。因此排空只用于明确降层数/
改变处理位置，不用于普通 F6；否则把每次热开关变成冷创建反而破坏体验。

### 自有纹理池还有可避免的增长

原 `FindOrCreateSlot` 在地址轮换时先占空槽，填满 8 槽后才复用空闲槽。
实际生产 selector 的 CPU 资源替身，在 100 次相同存储合同、已退休地址轮换下：前/后置均为
旧实现 8 次分配、候选 1 次分配，100 次都成功采用。**这不是“显存减少 87.5%”的游戏结论。**
忙碌、未知 fence、device-removed、immutable binding 等反例继续增长必要容量或保留旁路。

## 实现

1. `nr_pipeline_cache.h` 和 `EnsureCodec`：只缓存七个插件 codec PSO。key 包含适配器、驱动版本、
   node、flags、实际序列化 root signature、实际 shader 字节。SHA256/版本/严格长度核验；单文件
   16MiB、总 128MiB、128 文件上限；非阻塞进程间写锁，完整临时文件关闭后原子替换。只写
   `%LOCALAPPDATA%/DLSS5Core/PipelineCache/v1` 自有缓存，不写游戏目录、不保存 GPU 指针或 NR 模型。
   缺失/不可写/损坏/驱动拒绝时回原始创建；OOM、device removed/reset 不做重型第二次重试。
   INI `PipelineCache=0` 可在下一次进程/codec 初始化禁用，不改变运行中的 NR 参数。
2. `FindOrCreateSlot`：精确现有资源+motion 匹配仍优先；其次尝试同格式/物理范围、安全已退休的
   空闲存储重绑，再申请空槽。沿用 `CanMutateSlotLocked` / `InvalidateSlotBindingsLocked`。
   不改变 NR 实例、独立历史或最大在途容量，不因为更省显存跨层共享时域历史。
3. `ReconcileNrResidencyLocked`：显式降层数或修改 `processing_start` 时标记全旧自有 NR context
   退休，包括未活动路线；INI 重载和 UI 提交使用同一入口。关闭 NR 时也会标记，不依赖下一次
   `Prepare()`。F8 暂态配置不触发，普通 F6 同层数/同位置不触发，自动路线回退不当作用户改位置。
4. 全局排空屏障：在现有 `DeferNrCreationForReleaseLocked` 上补齐跨 carrier 检查；已退休仍在表内、
   待锁外销毁、Release 执行中任一存在都不能重建。新目标只在每个旧依赖自己的 recording/binding/
   fence 条件都完成且真实 Release 返回后创建。快速重新选择不撤销已退休对象，不创建过期目标。
   持续等待仍提示 F8/BUG；quarantine 不能为降占用而解除。

未改原生 SR/RR/FG、Color/MV/Depth/jitter/exposure/HDR、工作比例和画质参数，未默认预创建所有层。
本批没有通用低显存自动降层或完整 host-commit/VRAM 容量预测，不能称所有 OOM 已修。

## 已做验证及证据

证据仅保存在 DESKTOP 私有目录 `F:/ChatGPT/DLSS5/evidence/293-298/`，不提交 DLL/原始反馈。

| 检查 | 本批结果 |
|---|---|
| `residency-cpu-r3` | 基线/候选各 241 断言；实际 selector+fence 谓词；忽略 fence 的负控正确失败 |
| `residency-drain-r2` | 继承 10 寿命阻碍、240 表位置、200 四层级联；新增跨 carrier/待销毁/锁外 Release/F6/OFF 降层/快速重选/隔离通过；删除全局屏障负控正确失败 |
| `context-release-r1` | 旧 ready-retirement、旧 in-flight 两个负例复现；当前 Find/collector/drain 全部通过 |
| `model-boundary-r1` | 15 个真实 Core 合同/退休函数 CPU 场景通过 |
| `pipeline-cache-r2` | WARP 218、NVIDIA 225 断言；各 4 次真实 shader 输出一致，实际 miss/hit/改 shader miss/hit；去掉 checksum 验证的负控正确失败 |
| `beta7-cold-real-5layers-r1` | 冻结 Beta7 真实 Create/Release 分段和独立句柄；无 Evaluate |
| `cache-candidate-miss-r3` / `cache-candidate-hit-r1` | EnsureCodec-only 受控插入、7 PSO 缓存及完整排空重建一层；不是本分支整包验收 |
| `warm-driver-original-1` / `warm-driver-cache-1` | 热驱动相同输入对照，418.019 / 92.589ms codec；只有一对，不给普遍比例承诺 |
| 规范完整双语生成/编译、完整 Core GPU、游戏 | 另附最终构建回执；以上表格不能替代 |
| 托管 CI | 未启动/未验证；不启用付费工作流 |

`build-unified-candidate.ps1` 已接入新 slot/drain/cache 文件与 WARP 门禁；CPU 替身和独立 PSO
GPU 输出不冒充完整游戏链路。

### 可复现入口

- `python scripts/validate-residency-policy.py --compiler <clang++.exe> --output <fresh>`
- `python scripts/validate-residency-drain.py --compiler <clang++.exe> --output <fresh>`
- `python scripts/validate-pipeline-cache.py --compiler <clang++.exe> --output <fresh> [--hardware]`
- `python scripts/validate-coldstart-residency.py --core <frozen-generated-core> --compiler <clang++.exe>
  --reshade <source> --nr-runtime <signed-dll> --output <fresh> --run --layers 5 --compact-cycle`
- PSO 因果隔离另加 `--candidate-codec src/nr_before_sr.cpp --cache-root <private-common-appdata>`；
  必须注明它是函数级覆盖宿主，不是新完整 Core。所有输入对象和 helper 从冻结 source_commit 验证。

### 保留的失败/纠正

早期冷宿主误用 main 的旧链接 helper，遗漏 Beta7 private-SR 对象；之后错误假设 private-SR
receipt schema。它们是验证接线错误，**不是厂商 DLL 或既有模块被损坏**。最终改用冻结提交的
实际 helper，完整检查对应源码/八 shader/五对象。一次旧 main 完整 SHA 写错、一处工具生成
Python 换行转义错误、MAXULONG/WIDL fixture 定义问题及重命名旧测试 main 后缺显式 return
均有原失败日志，修正后从新目录重跑，没有降低断言。早期预检在收据建立前失败时只有终端
记录，不能声称所有失败都有独立 JSON。

## 游戏验收与剩余风险

同版运行库、同游戏场景/视角/每层实际尺寸，冷进程 OFF→ON、同配置热 F6，分别记录完整
停顿与首次可见时间。不要用延迟启用/持续旁路计算提速。分别测试无缓存、命中、Cache=0，
驱动/版本升级或只读目录可正常回退。

显存做一层→三层→五层→一层、五层→F6 OFF→改一层→ON，以及前/后置往返；核验旧闭包
真实排空、只创建最新目标、目标链恢复、热 F6 不增加 Create。RR/FG、尺寸变化/菜单/退出/
设备移除、未知提交与异常 Create/Release 的隔离继续覆盖。

显式降层/切位置现在会支付一次释放与重新创建成本；保留保真历史身份优先，不用异步不安全
Create 掩盖停顿。PSO 缓存只减少其覆盖阶段，首次没有缓存和厂商初始化的剩余延迟仍明确保留。
持续无效果、闪烁、拖影、旁路或崩溃应回滚并上传同期 F8/Core 日志及哈希。#293/#298 在游戏
验收和全链显存趋势确认前保持开放。

## 公开接口依据

- Microsoft, `ID3D12PipelineState::GetCachedBlob`:
  https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12pipelinestate-getcachedblob
- Microsoft, `D3D12_CACHED_PIPELINE_STATE`（blob 依赖硬件/驱动，其他 PSO 字段须匹配）:
  https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_cached_pipeline_state
- Microsoft, `IDXGIAdapter::CheckInterfaceSupport`（IID_IDXGIDevice / UMD version）:
  https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiadapter-checkinterfacesupport
