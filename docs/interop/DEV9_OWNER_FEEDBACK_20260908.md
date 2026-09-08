# dev9: readable controls and accurate RR colour status

Actual build source: `11f7dd00dbdd677e2f94bf90bda243c3052ec0d0`.
This successor preserves #192 at `8a82fb5`; no merge into main or stable release.
Owner feedback was collected on the exact dev8 Chinese Core SHA256
`447cef373215f9d376abcfc094ee8c3c19babd72dad76ec4177a4860a3c6fc7f`.
Private reports, game paths and raw logs remain outside public source.

## Findings

Cyberpunk owner reports no apparent problem. In the supplied interval, RR calls
increased by 19, NR success and composition submission by 17. This supports
actual activity on that configuration; it is not a universal quality claim.

NTE RR attempts increased by 45 while NR recordings remained zero; all 45
declined during preparation. The matching game log explicitly declares
`rr_depth_type=0` (linear). The existing path requires hardware depth. The game
RR succeeds, but Core NR is correctly not inserted without a conversion.
The old menu nevertheless described a selected processing route in a way that
could be mistaken for successful NR. The new display identifies this blockage.

The colour status also compared a recorded effective Mode1 frame with saved
Mode2. `EffectivePictureMode` now applies the same RR route observation to both
snapshot and UI checks, while retaining the user's saved preference. A real
extracted-policy regression covers RR entry, current post output, RR exit and
waiting for a new front frame. HDR permission is now a separate developer
setting; moving the colour slider no longer toggles experimental admission.

The owner confirms skin strength is ineffective even with RR off. Logs show
independent skin values retained while local tone/structure edits retire and
recreate the model. This excludes an observed UI preference overwrite, not a
model/segmentation or visual defect. No independent face mask is supplied by
the final composition. Renaming/moving the uncertain skin control does **not**
fix or close #162; no skin-colour heuristic or global strength compensation is
introduced. A real segmentation input remains necessary for guaranteed
independent final face strength.

## Changes and remaining work

Primary labels/tooltips describe the effect and tradeoff in plain Chinese.
Algorithm/input/colour-field/layer/skin experiments move under Developer
options without resetting saved values. Highlight mapping stays under HDR.
High-strength protection already defaults on and remains on by default.
Detailed F8 observations stay available without exposing coefficients as
primary picture controls. The bilingual source and PE identity are dev9.

For NTE's next F8, a bounded CPU-only sampler copies ViewToClipMatrix through
the existing NGX pointer getter and ReadProcessMemory. It runs at most once per
second for one feature generation, rejects unreadable/nonfinite memory, and
publishes a sequence-checked atomic receipt. No address is exported, no GPU
readback is added, and a finite copy is explicitly not an accepted projection.
Depth format/layout/sign/units and conversion still need validation; linear
depth remains bypassed. The next owner test provides the missing actual input
instead of weakening the admission guard.

## Validation and artifact identity

Exact-source focused validation passed: production colour policy 164,851
checks; model/control and persistence regressions; actual Windows bounded
memory-copy negative cases; portable RR ordering/F8 tests; all production SM5
entries; bilingual Core/nrchain compile/link; 13 WARP composition groups.
The intentional wrong-default negative control was rejected. Earlier staging
test pin mismatches are retained as failed evidence; the final run passed.
Existing upstream compiler warnings remain. No dev9 game/NVIDIA/performance
result or hosted CI pass is claimed.

| Artifact / record | SHA256 |
| --- | --- |
| Source manifest | 0e9791560404f19fc1d284587c2a0073fe62111beb64b8b8cb08fe7b0bbbbfc4 |
| English Core, 1,431,552 bytes | 693f8c0cfa60b3d23c5f9c81fa5a3b09d861ff9a3820224cb2571b5bf2828362 |
| Chinese Core, 1,457,664 bytes | ea576cdf895743a901ee3e108bb4bfa22470548bc059c84d25dbb67d9bbd9b27 |
| nrchain, 8,192 bytes | 46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb |
| Final validation log | bb2846879a6debdfc3a6cda862d7cf2305395caecbf2bab2b1f25150d6ccc68b |

Reproduce with `scripts/validate-050-acceptance.ps1`, passing explicit fixed
upstream/toolchain/ReShade/baseline paths, a new OutputRoot, and RunWarp. ReShade
headers remain at API18 `4a50d1e`; the existing tested game runtime does not need
upgrading for this Core. BL3's separate Luma API20 requirement is unrelated.
[Owner test instructions](../testing/DEV9_CORE_ACCEPTANCE.zh-CN.md).
