# Issue #39 — mutouren 0.4.4 HDR light-protect blackline (packet-batch 20260906)

## Source pack
- local: `unpacked_mutouren_0.4.4_hdr_protect_blackline`
- cloud path: `bug-inbox/issue-39/`

## Environment (capture-info)
- exe: `Client-Win64-Shipping.exe`
- build: `beta0.4.4-test1`
- GPU: `NVIDIA GeForce RTX 5090 D` driver `61656`
- display: `3840x2160` api=`49152`
- requested: enabled=`1` mode=`2` config=`30` blend=`1.10000002`

## Log identity
- build_id: `beta0.4.4-test1`
- sourceCommit: `1a8490a2c76d93b0c5e4cc50b8d4f178df55eb83`
- enabled/mode (from Config lines): enabled=`0` mode=`2`
- stage (last): `C-NR-`

## Judgment signals (post-enable / trailing)
| Signal | Value |
|---|---|
| Feature18-success | **6588** |
| transfer-prepared / recorded / bound / submitted | 6561 / 6561 / 6561 / **6561** |
| carrier-requests (last contract) | 6600 |
| queue-affinity admitted | 7507 |
| observerFault | none |
| Final NR bypass | **`native-hooks-unavailable`** |
| Bypass reasons seen | `native-hooks-unavailable` |
| Early native-hooks-unavailable | True |

## Owner note (important for cloud agents)
Startup `installing=1` / early `native-hooks-unavailable` is **NOT** a terminal failure.
Judge by **post-enable trailing status**: final bypass reason, Feature18-success, and transfer-*/submit counts after enable settles.

Refresh from mutouren_0.4.4_hdr_protect_blackline nested feedback.

## Files in this folder (cloud-readable)
- `capture-info.txt`
- `manifest.json`
- `nr-current.excerpt.txt`
- `nr-current.key-lines.txt`
- `nr-previous.excerpt.txt`
- `reshade.excerpt.txt`
