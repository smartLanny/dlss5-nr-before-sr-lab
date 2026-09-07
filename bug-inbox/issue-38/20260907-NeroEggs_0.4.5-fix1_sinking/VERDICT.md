# 20260907 NeroEggs 0.4.5-fix1 沉没之城2

- Outer SHA256: 90A071E68D0F418AA5B5F0CF1A3B97E1EA84FCF9F161F487F6236AADE782399C (98756 B)
- Source: NeroEggs via QQ bot
- build=beta0.4.5-fix1 sourceCommit=e5ce1210… verified / RTX 5080 / TheSinkingCity2.exe
- QQ: 前置显示生效但无明显画面差；后置不生效

## Log verdict
mode=2: Feature18-success→5049, but early ColorDiag maxFinalRelativeDelta≈0 / finalChanged=0 (HUD「生效」with near-zero visible delta); later ColorDiag meanFinal≈0.5%.
mode=1 capture end: null-guard-graphics-state-used dynamic=0x8; scope nrSuccess=0; post-SR stopped, no Feature18.
