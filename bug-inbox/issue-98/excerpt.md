# Issue #98 — 燕云 树海 (packet-batch 20260906)

## Source pack
- local: `unpacked_shuhai_feedback_a`
- cloud path: `bug-inbox/issue-98/`

## Environment (capture-info)
- exe: `yysls.exe`
- build: `beta0.4.4-test1`
- GPU: `NVIDIA GeForce RTX 5080 Laptop GPU` driver `61664`
- display: `2560x1440` api=`49152`
- requested: enabled=`1` mode=`2` config=`19` blend=`1.38999999`

## Log identity
- build_id: `beta0.4.4-test1`
- sourceCommit: `1a8490a2c76d93b0c5e4cc50b8d4f178df55eb83`
- enabled/mode (from Config lines): enabled=`1` mode=`2`
- stage (last): `C-NR-`

## Judgment signals (post-enable / trailing)
| Signal | Value |
|---|---|
| Feature18-success | **4136** |
| transfer-prepared / recorded / bound / submitted | 4128 / 4128 / 4128 / **4128** |
| carrier-requests (last contract) | 4200 |
| queue-affinity admitted | 409 |
| observerFault | none |
| Final NR bypass | **`native-hooks-unavailable`** |
| Bypass reasons seen | `native-hooks-unavailable`, `null-guard-graphics-state-used` |
| Early native-hooks-unavailable | True |

## Owner note (important for cloud agents)
Startup `installing=1` / early `native-hooks-unavailable` is **NOT** a terminal failure.
Judge by **post-enable trailing status**: final bypass reason, Feature18-success, and transfer-*/submit counts after enable settles.

Pack: shuhai_feedback_a (燕云).

## Files in this folder (cloud-readable)
- `capture-info.txt`
- `manifest.json`
- `nr-current.excerpt.txt`
- `nr-current.key-lines.txt`
- `nr-previous.excerpt.txt`
- `reshade.excerpt.txt`
