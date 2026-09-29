# 0.5.2 Beta 13

产品源码 / Product source: `cd8ba7009d333f7b03c463dc4237a913f9913081`.

功能说明缩短，详细解释悬浮查看；状态和计数继续直接显示。相对 Beta12 无渲染算法、默认设置或公开 ABI 改动。两份包分别搭配 Kio 1.0.6（40/50）和 plain FP16（20/30），中文默认，英文单独替换；保留已有设置。

Feature descriptions are shorter, with details on hover. Status and counters remain visible. There are no rendering-algorithm, default-setting or public-ABI changes from Beta12. Separate packages pair the same bilingual Core with Kio 1.0.6 for RTX40/50 or plain FP16 for RTX20/30. Chinese is the default; English is a manual alternative. Existing settings are preserved.

完整双语构建及两套运行库在 5090D 的受控接口短测通过。相应20/30显卡与各游戏仍需实测；Esc过渡、长期内存、私有SR和降噪质量门没有在这一版宣称修复。

The full bilingual build and controlled NGX smoke tests with both runtimes passed on RTX5090D. Matching RTX20/30 hardware and game acceptance remain separate. This release does not claim to fix the Esc transition, long-run memory growth, or the private-SR/denoising quality limitations.

详见 / Details: [new-PC handoff](BETA13_NEW_PC.zh-CN.md), [known issues](BETA13_KNOWN_ISSUES.zh-CN.md), [delivery evidence](../interop/052_BETA13_DELIVERY.json).
