# Pending — 红色沙漠 / Crimson Desert（owner zip 2026-09-09）

- **Game**: Crimson Desert (`CrimsonDesert.exe`) — 中文名红色沙漠 / Red Desert
- **Exe**: `D:\steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe`
- **GPU / driver**: NVIDIA GeForce RTX 5070 Ti Laptop GPU · driver **61664** (ReShade: 616.64)
- **Display**: 2560×1600 · api=49152 (D3D12 path via ReShade dxgi)
- **Owner symptom (zip filename)**: `0.3.3.5开启不变，0.4系列闪退` — 0.3.3.5 能开但画面无变化；0.4 系列闪退
- **Bundle**: DLSS5Feedback-v1 / DiagnosticBundle-v1 · session `DLSS5_FB_29380_9046750` · capture ~2026-09-09 01:17 CST
- **F8 TXT**: **无**（本包为反馈诊断包；`capture.crashDump=not generated`）
- **GitHub**: 尚未开 issue（pending；交由 parent / DLSS5 仓库）

## Versions contrasted (same zip)

| Role | Build string | Evidence |
|------|--------------|----------|
| **nr-current** + capture-info | **beta0.3.3.5** | `DLSS 5 AI Rendering Upscaling Edition beta0.3.3.5` · ReShade addon v0.3.3.5 |
| **nr-previous** | **beta0.4.5-dx11-compat** | `…Perfect Chain beta0.4.5-dx11-compat` · sourceCommit `dccb5b39…` dirty |

## Symptom vs logs

### 0.3.3.5（current）— 「开启不变」
- `requested.enabled=1` mode=2，但 `start/end.nrSuccess=0`，`bypass=rr-safe-bypass`
- Perf 长期停在 **`A1-native-RR-bypass`**；`feature18Evals=0`；`stages` missing（「NR disabled or no successful NR frame yet」）
- 用户多次 F6 / ReShade overlay 开关 Enabled，状态在 `rr-safe-bypass` ↔ `master-disabled` ↔ `waiting-feature1` 间切换，**始终无 Feature18 / 无 NR 成功帧** → 与「开了等于没开」一致
- colorPolicy: `no-combine-observed` / `unknown-live-contract`

### 0.4.5-dx11-compat（previous）— 闪退线索
- 启动后 RR Create/Evaluate 正常；`bypass: waiting-feature1 → rr-safe-bypass`
- 反复 **`NR bypass: reason=native-hooks-unavailable`**（installing=1），再经 `command-state-unverified` / `queue-affinity-unverified`
- 约 01:14:03 进入 **`none/active`**，写出 NR guide/create contract，并打出 **`Feature 18 create-time tuning: mode=1 extent=2560x1600 …`**
- **日志在 Feature 18 create-time 行戛止（无后续 Evaluate/成功帧）** — 与 owner「0.4 系列闪退」高度吻合；本包无独立 crash dump

## Notable lines

**nr-current (0.3.3.5)**
```
DLSS 5 AI Rendering Upscaling Edition beta0.3.3.5 attached | … | build_id=beta0.3.3.5
Config: enabled=1 mode=2 … feature18Evals=0
[state] bypass transition #1: waiting-feature1 -> rr-safe-bypass.
[perf] stage=A1-native-RR-bypass epoch=2 …
```

**nr-previous (0.4.5-dx11-compat) → crash tip**
```
…Perfect Chain beta0.4.5-dx11-compat attached | build_id=beta0.4.5-dx11-compat
[command-state] NR bypass: reason=native-hooks-unavailable … installing=1 …
[state] bypass transition #5: command-state-unverified -> none/active.
NR create contract: mode=1 extent=2560x1600 …
Feature 18 create-time tuning: mode=1 extent=2560x1600 intensity=1.000 …   ← log ends
```

**ReShade**
```
Registered add-on "DLSS 5 AI渲染超分版 beta0.3.3-dev …" v0.3.3.5 using ReShade API version 18.
Running on NVIDIA GeForce RTX 5070 Ti Laptop GPU Driver 616.64.
ERROR | IDXGISwapChain::ResizeBuffers failed with error code E_INVALIDARG!  (resize path; not the primary NR signal)
```

## Staged paths

- Pending (KEY + excerpts): `/workspace/napcat/bug-collector/bug-reports/bug-inbox/pending-red-desert-20260909/`
- Inbox full zip+extract: `/workspace/napcat/bug-collector/bug-reports/_inbox/20260909-red-desert-0335-vs-04crash/`
- ROG source: `C:\Users\pc1\Downloads\红色沙漠0.3.3.5开启不变，0.4系列闪退.zip`（见 `ROG-PATH.txt`）

## Routing note for parent
- 单包双版本：current=无效果(RR-safe bypass)，previous=疑 Feature18 create 闪退
- 建议开 issue 标题方向：`[Crimson Desert][0.3.3.5 no-op / 0.4.5-dx11-compat crash@Feature18]`；勿与 HD2 #147 混绑
