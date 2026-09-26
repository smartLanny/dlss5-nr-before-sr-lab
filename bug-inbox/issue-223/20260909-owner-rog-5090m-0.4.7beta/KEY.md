# SOTTR · 主人 ROG 5090M · 0.4.7beta 仍无效

- exe=`SOTTR.exe` · GPU=**RTX 5090 Laptop** · driver **61062** · build=**0.4.7beta**
- requested.mode=**1** · nrSuccess=0
- bypass=**module-observer-uncertain** · moduleObserverFault=**unknown-evaluate**
- 过渡反复：waiting-feature1 ↔ module-observer-uncertain ↔ master-disabled（体感「一直等 AI 帧安全旁路」）
- 日志：RR ownership tracking became uncertain during direct late-core Feature Evaluate；non-null handle before late `_nvngx` observer；All future Feature 1 processing fails closed
- compat.note：`Evaluate only; post admission remains required; no Present NR provider`
- **结论**：与 #223 既有 750 Ti 样本同病；**不是 Maxwell 门控**，50 系主人机能复现 → 升优先级

对照：0.5-dev14 上 RR 旁路已走出 RR-NR 兼容；本游戏却卡在 observer 安全旁路且后置未接管。
