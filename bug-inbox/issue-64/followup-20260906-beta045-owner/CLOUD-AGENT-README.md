# Cloud agent notes — #64 follow-up (Elden Ring + ERSS / beta0.4.5 Mode1)
eldenring.exe / RTX 5080 / driver 61656 / Mode1 post-SR NR.
Owner: pre-NR works on 0.4.5; post still “未满足NR安全插入条件”.
TAIL: end.bypass=command-state-unverified; reason=null-guard-graphics-state-used; dynamic=0x8; post-SR stopped, no Feature18.
module includes ERSSReShadeStub.addon. Early installing alone is NOT terminal — null-guard at post-SR is.
Related: #38 sinking city post null-guard; #117 limited blend 0x8 for pre path — verify whether post path still blocked.
