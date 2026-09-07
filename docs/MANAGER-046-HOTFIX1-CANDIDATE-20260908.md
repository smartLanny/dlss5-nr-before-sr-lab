# Manager 0.4.6-hotfix.1 候选交接

接续已发布 Manager `fb96c022b0a86e72da49a3a59778517daa7e0126`，不重复实现兼容桥接。
本轮准备独立候选、便携包与发布草稿；原公开 beta0.4.6 assets/tag 和产品分支发布基线保留。

## 已接入

中文核心实际构建源码 `35ef9a826642e0eabcecd46d012167dd52b98105`，Core PE 0.4.6.1。
Manager 产品版本 0.4.6-hotfix.1、Windows 文件版本 0.4.6.1，核心与安装器来源分别记录。

新装默认槽为 0.4.6-hotfix.1，保留 0.4.6/0.4.5 及历史核心槽原字节用于回退。
OTA 接口以显式版本 profile 记录 metadata、核心名称与三件套哈希，保留旧包支持，拒绝未知版本、原型键、跨版本配套和自报哈希的未审核文件。

DXGI/API 选择、按 EXE 保存、DX12 不部署 carrier、DX11 配套 carrier、桥接手动关闭、nrchain 保留、ReShade 精确排除和 journal 回滚均沿用已发布规则。

## 默认倍率与说明

新候选 INI 中 TransferStrength=1.00、PostTransferStrength=1.00。现有 INI 在安装修复、OTA、版本切换中保留。
Manager 原恢复默认只写模型强度等公开设置，未重置这两个最终倍率；本轮补齐，两键均回到 1。
普通色调跟随调整仍保留显式最终倍率。Manager 其余产品默认沿用既有值，不声称整组等同于核心“恢复推荐”。

界面将模型皮肤结构参数和 NR 色调跟随用途写清；模型内部参数不是最终合成的人脸遮罩，色相/饱和度跟随也不是整体效果倍率。
新核心修复主面板 F8/F7 编译排除及独立反馈窗口空白，合并反馈/诊断。采集后端和热键没有被删除。

## 固定身份

| 组件 | SHA256 |
|---|---|
| 中文核心（1368064 bytes） | `0727be26ceddcf60354535cee7c12a3138eef3075d7f90110b3693508fb633a5` |
| DX11 carrier | `4656d9aac382a6f9b5c8488669aa5365283f03b5b94b2267f1c7f9b53927dc86` |
| 共用 nrchain | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |
| 默认 INI | `469f47219e170443a5887c4614f20452469df8e7ad954fb80f10d4fb4c1e6f01` |
| 中文 OTA（685762 bytes，7 文件） | `bd0ebd2f94041a7fee73aa4d77f48c914b833a0eec9f05b12e41419281e5dc77` |
| 新 bundle | `cab5c5e0ded47e30c695c3ad1adeba59a28d021495ea31f7b47d815d2a1d1bb8` |

`prepare-release-046-hotfix1.js` 在复制前验证完整批次；`verify-payload.js --write` 保留独立新槽并校验实际 pins。
核心 source manifest 为 bf69c537c4c20b75b68940463d4990742f95d17ddffbdd5a7bbe19688c51f90d，validation 为 62dfd5ac613c3d14575d3f9a6de4e13a7fe9f929608baba8fd133d80e0224da2。

## 验证边界

Manager 冻结源码全量 151/151 通过、零跳过，包含真实 PR160；UI contract 通过。
实际 hotfix payload/OTA 临时目录验证 DX12/DX11、桥接关闭/启用、回退 0.4.6、INI 保留、manifest 写失败 journal 回滚和主动默认恢复。
首份便携候选在源码比较中被发现缺少最后追加的 OTA profile 自有属性检查，未上传；冻结后重建，最终交付只选 final-dist 产物。
本地证据位于 build/hotfix1-final-tests.log、build/hotfix1-final-ui-smoke.json、build/hotfix1-final-artifact-verification.json 及 packaged 测试记录。

核心交接的 40 项 Windows 编译/WARP 门禁和实际双语 DLL 控件检查已按源码与产物身份核对。
这不是实际游戏或 NVIDIA Feature18 验收。没有新的人脸独立 mask/倍率实现，也没有新 carrier 崩溃源码修复，不宣称人脸问题全部修复。
公开草稿只含中文 OTA、便携包及脱敏身份，不含原始玩家 F8/图像或本地工具安装路径。


最终便携包：DLSS5-Manager-0.4.6-hotfix.1-portable.exe，306169461 bytes，SHA256
`d45a007f2a277bf4f86c6ddaf8c424b17cb1d387f8ecc1d354e1df2963aee5a6`。
最终 EXE 内 37 个应用源码文件与完整载荷逐一一致，12 组归档选择通过；成品提取代码的 28 项部署/默认值/回滚/UI 测试通过，零跳过。
