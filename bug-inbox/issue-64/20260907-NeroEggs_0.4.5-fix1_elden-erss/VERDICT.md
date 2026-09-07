# 20260907 NeroEggs 0.4.5-fix1 艾尔登法环+ERSS

- Outer SHA256: 3234B15FD6D73A340D2AAAD95BBEF9E4013388C3366395648C2BCB6E02698ABF (112515 B)
- Source: NeroEggs via QQ bot (`_inbox/chat-20260907/elden.zip`)
- build=beta0.4.5-fix1 sourceCommit=e5ce1210… addonSha256=62e2cc84… verified
- GPU RTX 5080 / 61656 / eldenring.exe / ERSSReShadeStub.addon present
- QQ: 前置有效、后置不生效

## Log verdict
Same session: mode=2 (pre) Feature18-success climbs to ~2582 + transfers; ColorDiag shows real deltas.
Switch to mode=1 (post): immediate null-guard-graphics-state-used, dynamic=0x8; scope nrSuccess=0; applied.frame=2582 is historical pre success only.
