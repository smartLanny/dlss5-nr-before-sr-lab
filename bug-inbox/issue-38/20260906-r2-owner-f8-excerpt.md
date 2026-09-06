# 沉没之城2 / TheSinkingCity2 — beta0.4.3r2 F8 补充（owner 2026-09-06）

## Environment
- exe: `TheSinkingCity2.exe` (UE / UnrealWindow)
- build: **beta0.4.3r2** (`sourceCommit=dc876fc81ba831089f407f01a498e4b04d616122`, verified)
- GPU: NVIDIA GeForce RTX 5080, driver 61656
- Display: 3840x2160, D3D12
- requested: enabled=1, **mode=2** (C-NR-transfer), config=2

## Clearer judgment (r2 summaries)
Plugin hooks + DLSS SR work; **NR never admits**.

| Signal | Value |
|---|---|
| Feature1 Create | Success `2562x1441 → 3840x2160` quality=2 flags=75 |
| FrameGeneration Create | Success (id=11) |
| observerFault | none |
| Feature18-success | **0** (entire session) |
| transfer-prepared/recorded/bound/submitted | **all 0** |
| start/end nrSuccess | **0** |
| end.bypass | `command-state-unverified` |
| Steady NR bypass | **`null-pso-policy-disabled`** (dominant) |
| Early NR bypass | `native-hooks-unavailable` (warmup) |
| queue-affinity admitted | **0** (two D3D12 graphics queue identities observed; Feature18 needs stable per-CL affinity) |
| carrier-requests | high (~15k+) while transfer stays 0 |
| ShortFuse / RenoDX | not loaded |

## Previous log in same pack
`nr-previous-summary` is **beta0.4.3** mode=1 (B-post-SR-NR) with the same NR-bypass pattern.

## Takeaway for #38
This is not “plugin failed to load” and not hook ownership fail-closed. It is a **command-state / null-PSO policy gate**: SR carrier runs, Feature18 never succeeds, so guests see no NR. Aligns with earlier NeroEggs F8 on older builds.

Local copies: `20260906-r2-*.txt` / `20260906-r2-feedback.zip` in this folder.
