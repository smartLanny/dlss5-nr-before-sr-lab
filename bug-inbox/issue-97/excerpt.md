# Issue #97 — 异环 360 / HTGame (packet-batch 20260906)

## Source pack
- local: `unpacked_360_feedback_a`
- cloud path: `bug-inbox/issue-97/`

## Environment (capture-info)
- exe: `HTGame.exe`
- build: `beta0.4.4-test1`
- GPU: `NVIDIA GeForce RTX 4080 SUPER` driver `61664`
- display: `3840x2160` api=`49152`
- requested: enabled=`1` mode=`2` config=`22` blend=`1.20000005`

## Log identity
- build_id: `beta0.4.4-test1`
- sourceCommit: `1a8490a2c76d93b0c5e4cc50b8d4f178df55eb83`
- enabled/mode (from Config lines): enabled=`0` mode=`2`
- stage (last): `C-NR-`

## Judgment signals (post-enable / trailing)
| Signal | Value |
|---|---|
| Feature18-success | **0** |
| transfer-prepared / recorded / bound / submitted | 0 / 0 / 0 / **0** |
| carrier-requests (last contract) | 3000 |
| queue-affinity admitted | 0 |
| observerFault | none |
| Final NR bypass | **`native-hooks-unavailable`** |
| Bypass reasons seen | `native-hooks-unavailable` |
| Early native-hooks-unavailable | True |

## Owner note (important for cloud agents)
Startup `installing=1` / early `native-hooks-unavailable` is **NOT** a terminal failure.
Judge by **post-enable trailing status**: final bypass reason, Feature18-success, and transfer-*/submit counts after enable settles.

Pack: 360_feedback_a (异环).

## Files in this folder (cloud-readable)
- `capture-info.txt`
- `manifest.json`
- `nr-current.excerpt.txt`
- `nr-current.key-lines.txt`
- `reshade.excerpt.txt`
