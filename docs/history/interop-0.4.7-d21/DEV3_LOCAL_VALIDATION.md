# dev3 精确源码本地验证记录

构建源码：`b03f474ba5127ab057fbb5e93708c47eb9dbb866`。版本`beta0.4.5-multiapi-dev3`，核心PE `0.4.5.103`。本文是构建后的证据文档，后续文档HEAD不替代该二进制源码身份。

## 实际通过

2026-09-07在Windows本机运行 `scripts/build-multi-api-candidate.ps1`，输出批次`dev3-pass12`，进程退出0。随后同一源码HEAD运行`package-multi-api-candidate.ps1`退出0，并用独立Python ZIP reader重新核对CRC、所有内部SHA256及完整文件集合。

| 验证 | 结果/边界 |
|---|---|
| 唯一生成入口、固定依赖与源文件哈希 | 完整staging通过；Bridge固定`28aed4099b0fe1c207b20b5fee5364c0773c25c2` |
| Python变换/合同/实际Windows检查 | multi_api 66、native_bridge 16项通过，无skip |
| 实际carrier生产完成/退役函数 | 35状态断言通过；fake queue只作为错误路径接缝 |
| 实际GPU完成等待 | WARP真实queue/fence完成、超时、不伪造completion通过 |
| 硬件异常传播 | Create/Evaluate子进程RaiseException(0xc0000005)退出该码；普通返回码不改写 |
| 实际生成RR三Release入口 | 54场景通过；故意恢复仅Feature1退役时负控准确失败 |
| 颜色/模型默认、推荐、预算 | 分别165、336、170254检查通过 |
| 实际曝光view/设备能力适配器 | 8252断言通过；另有颜色/MV/ABI等生产策略检查 |
| F8/core反馈 | 130项实际Windows采集检查及独立archive解析通过；不调用NR/图像/GPU seam |
| 命令状态 | 原生观察、64KiB工作线程/栈门禁、24场景WARP恢复通过；错误blend真实像素负控通过 |
| 后置处理 | 6个实际生产shader入口编译；48场景工作尺寸/原图快照/修改回填WARP通过 |
| 静态分析 | 实际staged英文、中文核心及nrchain零诊断 |
| Windows编译链接 | 英文核心、中文核心、nrchain和carrier全部通过；carrier有7条上游novtable属性忽略警告 |
| OTA | 中英各一个核心+同源chain+carrier；不含INI/NVIDIA DLL/模型；完整CRC/内层SHA256通过 |

WARP中的神经输出是确定性测试替身。上述检查不代表NVIDIA模型、某游戏视觉、FG、HDR/运动或长时性能通过；游戏基础测试另记。

包内`validation.json`由自动构建门禁生成，`game_runtime_verified=false`和`nvidia_feature18_verified=false`描述该脚本的范围。后续人工操作游戏的基础结果单独记录，不回写这两个字段冒充完整游戏/模型验收，也不改变已经验证的发行字节。

## 未通过/修复前失败保留

- 旧严格`post_sr_snapshot_gpu` harness要求D3D12 Debug Layer，本机缺失，返回`0x887a002d`。未删要求，也不以另一WARP测试替它宣称通过。
- 同一源码的GitHub Actions运行[34103506944](https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/34103506944)未启动执行步骤。check `101683145916` annotation明确账户付款失败或spending limit；实际jobs均无steps。**本地通过不是托管CI通过**。
- pass7：blend负控本来正确检出坏像素，但PS5把预期stderr转成终止异常；改为独立进程捕获并严格核对退出码/原错误后通过。
- pass8：临时stage目录rename返回WinError5，未产生可交付stage；保留失败日志并用新目录重跑。
- pass9：旧UI blob测试pin拒绝新布局；核对新UI后同步pin，未恢复旧源码。
- pass10：重复反馈测试复用CREATE_NEW报告路径导致Win32=183；测试改为每次独立目录，保留旧证据，生产写入保护未放宽。
- pass11：以上修复后完整执行通过；随后用346a74f进行了2077存档场景矩阵。
- pass12：游戏实测发现的旧RR性能标签改名后，对最终b03f474重新完整执行通过。日志中的故意坏blend/旧RR泄漏失败是通过的负控，不能单独截取当作生产失败。

## 发行字节

| OTA | 字节 | SHA256 |
|---|---:|---|
| `DLSS5-beta0.4.5-multiapi-dev3-Chinese-OTA.zip` | 679998 | `0943f0c3f2b314492d8ce05f9c4b350eccbe430a70a43ea4673629aaa54a18a7` |
| `DLSS5-beta0.4.5-multiapi-dev3-English-OTA.zip` | 667589 | `d65f2e951b21148058c9cae46f1bcd7242dd73b769d17f555c6b4c5a1a558f56` |

来源与二进制逐文件哈希在包内`source-manifest.json`、`validation.json`与`SHA256.json`。每包14个文件均由内层清单覆盖（SHA256清单自身除外）。维护者本机保留`nr045-unified-pass12.log`、打包日志、`evidence/dev3-release-20260907/delivery-verification-b03f474.json`及所有失败批次原日志。

F8里的`sourceDirty=1`由既有生成器明确写入，表示使用经过变换的实验staging源码（`prepare-native-bridge.py`）。构建前原始Git源必须提交且无tracked修改；变换后的每个文件由manifest单独锁定。本次二进制源码身份仍是上方完整SHA。
