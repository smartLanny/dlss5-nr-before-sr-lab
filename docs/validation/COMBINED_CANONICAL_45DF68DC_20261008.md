# 现有问题修复组合：完整 Windows 验证

2026-10-08。仅本地内部组合，未合 main、部署游戏、发布或改变 Manager。
此组合包含冻结 #338/#340、#341 退休事件配对修复、#343 账本与 CPU fixture
修正，以及 #342 研究接缝。新两 RR / 一次 RR / 官方场景测试台未接入产品。

| 身份 | 值 |
|---|---|
| 实际构建 commit | `45df68dc80138e89778e544d49f15109ec437a4f` |
| 实际生产 src tree | `7fe3819be240c0fad0eb16526e9d4b3ce0a67f87` |
| 版本 | `0.5.2-beta.16`，PE `0.5.2.16` |
| 源 manifest SHA256 | `fe0f7f9e86d2d9349bdd162c36c158009c7c0837f05929eac8d52bdd78e58e26` |
| 英文 Core SHA256 | `401c19ae7c4a23c185c251db332bb752a6547bc9ecc43ce8775d813360728375` |
| 中文 Core SHA256 | `65cedcd1c6843e2af9c6c3372512741912a346741da1da2d43a8b812f43218d9` |
| nrchain SHA256 | `1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2` |
| 英文内部 ZIP SHA256 | `ed6460ccb2c45b9751d320fbb84245e22ef4416cd5bb48df8b71e97380d0ffc2` |
| 中文内部 ZIP SHA256 | `696911c10a0b0787768652f408db9427bc94959d169932fbce378c30f673b591` |

从 clean committed tree 运行原 `scripts/build-unified-candidate.ps1`，包含完整
人脸 backend、实际 FFX SDK/cache、双语编译/LLVM static analysis、现有完整
CPU/WARP 门及独立反证；2026-10-07 20:02:56Z 开始，20:31:53Z 完成，exit0。
入口后的 source/HEAD 核对仍一致。明确传 `AllowMissingDebugLayer=true`，不声称
debug-layer 验证。没有把 optional/hosted skip 算通过。

依赖固定 carrier `28aed4099b0fe1c207b20b5fee5364c0773c25c2`、ReShade
`4a50d1eddace85734871d91792ff214f13f66c01`、DirectXMath
`28929ac6f3c1649a7641741b689ebecccb17aa8d`、LLVM23.1.0、ORT1.23.2、
FFX1.1.4 和既有精确源 nrchain/codec cache。运行没有更换游戏运行库。

原失败保留：r1 旧 slot fixture 编译错误；r2 的 O0 研究 fixture 在64KiB worker
上因大型临时对象栈溢出。r3 前只修已证明的 fixture 存储位置，并保留旧反例与
错误 layer-negative；生产 src tree 未变。没有跳过失败门或清零生命周期状态。

最终 wrapper receipt SHA256：
`449d7d663e9c3ed710184540bd78c1487190fb07a955c8f27d75c4820e260145`。
完整日志 SHA256：
`2b1988dfcd6b6475c4a8a3d3d051b475a455d3ee1df8347c570184459e01d38b`。

这里证明 source/build/CPU/WARP。实际 NVIDIA Feature18、真实场景采用、画质、
整帧性能和游戏验收分别后置；不能据此声称0.4.7帧数差已修好。官方 Sample
接入将使用此准确 Core 做新的隔离 tuple 首门，不继承066旧运行库实验结果。
