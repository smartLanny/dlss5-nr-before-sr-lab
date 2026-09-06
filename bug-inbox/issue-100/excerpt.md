# Issue #100 — 异环 呼呼 (r2/r3-era pack) (packet-batch 20260906)

## Source pack
- local: `unpacked_huhu_feedback_c`
- cloud path: `bug-inbox/issue-100/`

## Environment (capture-info)
- exe: `HTGame.exe`
- build: `beta0.4.3r3`
- GPU: `NVIDIA GeForce RTX 5060 Ti` driver `61664`
- display: `1920x1080` api=`49152`
- requested: enabled=`1` mode=`2` config=`42` blend=`1.02999997`

## Log identity
- build_id: `beta0.4.3r3`
- sourceCommit: `d42a8eb23778bc602d0d638193872d41216c2203`
- enabled/mode (from Config lines): enabled=`1` mode=`2`
- stage (last): `C-NR-`

## Judgment signals (post-enable / trailing)
| Signal | Value |
|---|---|
| Feature18-success | **7799** |
| transfer-prepared / recorded / bound / submitted | 7475 / 7475 / 7475 / **7475** |
| carrier-requests (last contract) | 7800 |
| queue-affinity admitted | 752 |
| observerFault | none |
| Final NR bypass | **`null-guard-graphics-state-used`** |
| Bypass reasons seen | `native-hooks-unavailable`, `null-guard-graphics-state-used` |
| Early native-hooks-unavailable | True |

## Owner note (important for cloud agents)
Startup `installing=1` / early `native-hooks-unavailable` is **NOT** a terminal failure.
Judge by **post-enable trailing status**: final bypass reason, Feature18-success, and transfer-*/submit counts after enable settles.

Pack: huhu_feedback_c — r2/r3-era; still extract for comparison.

## Files in this folder (cloud-readable)
- `capture-info.txt`
- `manifest.json`
- `nr-current.excerpt.txt`
- `nr-current.key-lines.txt`
- `nr-previous.excerpt.txt`
- `reshade.excerpt.txt`
