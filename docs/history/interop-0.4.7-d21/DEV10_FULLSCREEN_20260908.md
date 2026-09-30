# dev10: full-screen capture and owner feedback handoff

Source: `2f984d83b3f5c35d0af55d885d2124eae1aca370`, stacked on dev9
`9474a7905f7a3bf5ae3e93547a05a8a2e0b3cd63` / #197. Research remains on
the 0.5 line, without merging into main or publishing a release.

## Fixed capture failure

Two owner dev9 ShareMatrix archives completed only the off/current first group
and timed out in the 50% group. The overlay called `SnapshotOverlayConfig` →
`ReloadConfig` after 500 ms and replaced the transient resolution/config generation
with the disk settings. Capture admission then waited for a generation that was
no longer active. The Present poll already respected the hold; these readers did not.

The shared ReloadConfig entrance now respects the capture hold. The actual
production snapshot/reload entrance plus a disk backend seam reproduced the
failure before the patch and passed after it. Polling resumes after restoration.
The original partial archives and red/green logs remain private.

## Owner-visible changes

- F7 and the current/seven-group menu use full-screen runtime screenshots. Original
  image bytes are copied unchanged; no center crop, resize or raw-plane readback.
  Internal bounded raw capture remains available to its diagnostic harness.
- Each group carries requested/applied configuration JSON, including face control,
  strength, NR work sizing and post percent. Applied settings are not execution proof.
- Face adjustment returns to the normal picture page with the requested Chinese name.
  The underlying model parameter behavior is unchanged.
- Missing settings default to Enabled=1, Mode=2. Valid saved modes and an explicit
  disabled choice survive reload and legacy metadata migration.

## Exact local acceptance

`validate-050-acceptance.ps1 -RunWarp` passed 19 recorded gates. Key checks:
453 production capture/controller checks; 16 complete 3840×2160 PNGs byte-identical
after ZIP packaging with skin 0/2 recorded and no raw ROI planes; 88 actual Win32 INI
migration checks; actual bilingual face-control block (28) and skin policy (80);
SM5 compilation, full bilingual Core/nrchain link and 13 synthetic WARP groups.
No new NVIDIA Feature18/game/performance or hosted-CI claim.

| Artifact | SHA256 |
|---|---|
| Source manifest | `911827a4fcda70c36ea92c2526c0e6ba3e30dacf956d2a67ce8cff45e56387be` |
| Validation log | `f1df3bc3fcec63251583e000b60e974ff8a79ed5eacfe049df16dcfc2838b006` |
| Chinese Core | `aed930f3ca1de08a97fadbf0f2997c5c7ebe8bafe0e030dec32f4e11d4d10732` |
| English Core | `e09358017a1ffc0c1cb85495eabc72d753155be839f00969718f8f16292e99d3` |
| nrchain | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

## Known defects and issue decisions

- [#162](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/162) remains open,
  explicitly parked at the owner's request. NTE face control has no clear independent
  effect in current 0.5 or the exact 0.4.6 rollback, also with RR off. Other players'
  similar older-version observations are owner-reported, not independently reproduced.
  CP2077 effectiveness is also owner feedback. No 0.5 regression or game-only cause
  has been established. No further face-specific test requests in this batch.
- NTE post-route darkening is a separate observation. Prior #166 introduced bounded
  residual and post lighting protection; the owner feels the newer build darkens less.
  This is not a verified face-mask fix or an attributed A/B result.
- #99/#101: old hook/ordinary-SR fixes exist, but their later RR complaint remains open.
  The owner's dev8 RR report has 45 prepare declines, no corresponding NR/compose
  progress. dev9/dev10 observe projection and explain the actual state; they do not
  convert linear RR depth. Replies distinguish these scopes, without false closure.
- #106: returned teardown-error quarantine and bounded carrier safety changes are
  completed subitems. Full runtime hard-fault/session isolation is still an open RFC.
  It does not block this UI/capture delivery.

Issues #162/#99/#101/#106 were updated with these decisions on 2026-09-08.
Owner screenshots, process logs and install backups are local evidence only.
