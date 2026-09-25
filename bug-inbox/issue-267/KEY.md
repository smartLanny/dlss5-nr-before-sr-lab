# 木头人 鸣潮 D13 vs D18 对照（2026-09-13）

- QQ：`1575820653`（123大家都是木头人）私聊
- GPU：RTX **5090 D** · driver **61656** · exe=`Client-Win64-Shipping.exe` · api=49152
- 口述：D13 帧率正常；D18 一进游戏帧率掉；D13 独立人脸保护/独立脸区可用，D18 选项灰掉
- F8 不记 FPS；文件夹名「帧率129/89」仅为客人口述标记

## 材料注意
客人 zip 名都写了 `0.5lin13`，但其中「帧率89」包内 TXT 实为 **`beta0.5-dline18`**（误标）。

| 客人标注 | zip | 实际 build | 本地 F8 |
| --- | --- | --- | --- |
| lin13 帧率129 | `鸣潮 5090D  0.5lin13 帧率129.zip` | **dline13** | `260913-wuwa-dline13-fps129-mutouren.txt` |
| lin13 帧率89（实为 D18） | `鸣潮 5090D 0.5lin13帧率89.zip` | **dline18** | `260913-wuwa-dline18-fps89-misnamed-folder-mutouren.txt` |

## F8 快对照（两边 NR 都在跑）
- 两边：`requested.enabled=1` · `applied.valid=1` · `requested.faceMask=1` · `applied.autoMask=1` · `nrSuccess` 上涨
- **D13**：`commandState.dynamic=0x77` · `vertexMask=0x3F` · `guardCovered=1`
- **D18**：`commandState.dynamic=0x0` · `vertexMask=0x0` · `guardCovered=0` · `compatSource.decision=fg-consumed` · 多了 `compat.route`/`uiActivity` 字段
- 人脸灰掉：F8 仍见 `faceMask=1` 请求，属 **UI 可用性/门控** 口述+截图；不是 F8 把 faceMask 写成 0

## 本地路径
- box：`/workspace/napcat/bug-collector/bug-reports/_inbox/20260913-mutouren-wuwa-d13-d18/`
- Windows：`C:\Users\pc1\Documents\ChatGPT\DLSS5\bug-reports\_inbox\20260913-mutouren-wuwa-d13-d18\`（含 zip/`clean`/`ex*`）

## Issues
- 掉帧：**#266**
- 人脸灰掉：**#267**
