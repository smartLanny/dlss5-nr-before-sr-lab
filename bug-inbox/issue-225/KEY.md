# 2077（装模组）· 状态闪烁 / 时不时失效

## 现象（客人）
- 0.4.6 hotfix.1：时不时闪一下状态、时不时失效；曾有过不闪的 0.4.6 时段，后不知调了啥又闪
- 0.4.7beta：同样闪烁

## F8 摘要（同一机）
| 项 | hotfix.1 | 0.4.7beta |
| --- | --- | --- |
| exe | Cyberpunk2077.exe | 同 |
| GPU | RTX **5080** · driver **59649** | 同 |
| mode | 1 | 1 |
| start/end bypass | none/active · nrSuccess 高 | none/active · nrSuccess 高 |
| 关键点 | **none/active ↔ queue-affinity-unverified** 反复跳 | 同型反复跳 |
| ColorDiag | finalChanged≠0（NR 有改画面） | finalChanged≠0 |

启动瞬间 `native-hooks-unavailable/installing` → command-state-unverified 是过渡，**不是终态**。
会话里 queue-affinity `unknown-bypass` 有计数（hotfix.1≈23–25；0.4.7≈7）。

## 归类建议
- 新开「2077 模组场景 queue-affinity 抖动」或补 #124/#43 类闪烁族；勿当完全无效
- 挂 #124（2077 验收）交叉引用
