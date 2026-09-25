# Owner D20 test (5090 D · DESKTOP test PC) 2026-09-14

Oral:
- D20 异环/相关：闪烁好像少很多
- 异环：默认不加载 ReShade ≈117 FPS；加载插件后即使 F6 关也掉到约 100；开不同模式继续掉（主人认为似乎合理）
- 2077：默认不补帧约 77；装插件后即使关闭也低很多；NRSR 与 SRNR 帧率一样低，像被锁住

PresentMon summaries (GPU1 = dGPU util):
- yihuan baseline FG0: median ~116.7 FPS, GPU1 ~47%
- yihuan with plugin FG1: median ~91.5 FPS, GPU1 ~93%
- cp2077 with plugin FG1: median ~70.6 FPS, GPU1 ~97%
- cp2077 FG2 capture: median ~117.8 FPS, GPU1 ~57% (likely different FG/scene; not pure baseline)

F8:
- HTGame dline20: requested.enabled=0 → end.bypass=master-disabled, historical-success (F6 off sample)
- SOTTR dline20: module-observer-uncertain, nrSuccess=0 → supplement #223
