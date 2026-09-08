# 黎明行者 — 主人 5090 Laptop · beta0.4.6-hotfix.1 过测

- 来源：主人 2026-09-09 自测 F8
- exe=Dawnwalker.exe · GPU=RTX 5090 Laptop · driver=61062 · build=**beta0.4.6-hotfix.1**
- bypass=**none/active**（start/end）；hooksReady=1；nrSuccess 很高（scope~2000 / session 1.2万+）
- Feature18-success 与 carrier 接近（如 1799/1800）
- **ColorDiag finalChanged=2040 / 2030**（非 0）——与旧 0.4.6「合成无可见变化 / finalChanged=0」对照：**本机本版合成在变**
- colorFollowEffective=0 但 preserveLighting=1；style 0→1→2 切换有重建
- 启动瞬间有一次 native-hooks-unavailable/installing（非终态）
- 文件：`260909-dawnwalker-beta0.4.6-hotfix.1-022704123-72792-2.txt`
- 建议：#177 补「hotfix.1 + 5090M 过测有效」评论；问阿龙同版复测观感

