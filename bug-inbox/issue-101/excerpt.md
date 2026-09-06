# Issue #101 — 异环 王牛奶 (packet-batch 20260906)

## Source pack
- local: `unpacked_wang_feedback_small`
- cloud path: `bug-inbox/issue-101/`

## Environment (capture-info)
- exe: `HTGame.exe`
- build: `beta0.4.4-test1`
- GPU: `NVIDIA GeForce RTX 5080` driver `61664`
- display: `3440x1440` api=`49152`
- requested: enabled=`1` mode=`1` config=`16` blend=`2`

## Log identity
- build_id: `beta0.4.4-test1`
- sourceCommit: `1a8490a2c76d93b0c5e4cc50b8d4f178df55eb83`
- enabled/mode (from Config lines): enabled=`0` mode=`2`
- stage (last): `B-`

## Judgment signals (post-enable / trailing)
| Signal | Value |
|---|---|
| Feature18-success | **5600** |
| transfer-prepared / recorded / bound / submitted | 6125 / 5580 / 5580 / **5580** |
| carrier-requests (last contract) | 3 |
| queue-affinity admitted | 25 |
| observerFault | none |
| Final NR bypass | **`null-guard-graphics-state-used`** |
| Bypass reasons seen | `native-hooks-unavailable`, `null-guard-graphics-state-used` |
| Early native-hooks-unavailable | True |

## Owner note (important for cloud agents)
Startup `installing=1` / early `native-hooks-unavailable` is **NOT** a terminal failure.
Judge by **post-enable trailing status**: final bypass reason, Feature18-success, and transfer-*/submit counts after enable settles.

Pack: wang_feedback_small.

## Files in this folder (cloud-readable)
- `capture-info.txt`
- `manifest.json`
- `nr-current.excerpt.txt`
- `nr-current.key-lines.txt`
- `nr-previous.excerpt.txt`
- `reshade.excerpt.txt`
