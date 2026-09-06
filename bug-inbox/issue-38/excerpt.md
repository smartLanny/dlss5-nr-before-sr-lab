# Issue #38 — NeroEggs 0.4.4test1 sinking (packet-batch 20260906)

## Source pack
- local: `unpacked_NeroEggs_0.4.4test1_sinking`
- cloud path: `bug-inbox/issue-38/`

## Environment (capture-info)
- exe: `TheSinkingCity2.exe`
- build: `beta0.4.4-test1`
- GPU: `NVIDIA GeForce RTX 5080` driver `61656`
- display: `3840x2160` api=`49152`
- requested: enabled=`1` mode=`1` config=`25` blend=`1`

## Log identity
- build_id: `beta0.4.4-test1`
- sourceCommit: `1a8490a2c76d93b0c5e4cc50b8d4f178df55eb83`
- enabled/mode (from Config lines): enabled=`1` mode=`2`
- stage (last): `B-`

## Judgment signals (post-enable / trailing)
| Signal | Value |
|---|---|
| Feature18-success | **0** |
| transfer-prepared / recorded / bound / submitted | 2 / 0 / 0 / **0** |
| carrier-requests (last contract) | 3 |
| queue-affinity admitted | 740 |
| observerFault | none |
| Final NR bypass | **`null-guard-graphics-state-used`** |
| Bypass reasons seen | `native-hooks-unavailable`, `null-guard-graphics-state-used` |
| Early native-hooks-unavailable | True |

## Owner note (important for cloud agents)
Startup `installing=1` / early `native-hooks-unavailable` is **NOT** a terminal failure.
Judge by **post-enable trailing status**: final bypass reason, Feature18-success, and transfer-*/submit counts after enable settles.

Refresh from NeroEggs_0.4.4test1_sinking nested feedback.

## Files in this folder (cloud-readable)
- `capture-info.txt`
- `manifest.json`
- `nr-current.excerpt.txt`
- `nr-current.key-lines.txt`
- `nr-previous.excerpt.txt`
- `reshade.excerpt.txt`
