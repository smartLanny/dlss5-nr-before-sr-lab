# Approximate two-RR: bounded actual execution

This follows the independently rechecked Opti denoise-first sequence: raw R → private DLAA RR at R → single-layer Feature18 NR → ratio-before-shift edit on original raw R → original parent RR at O. Clean color is zero-jitter; its reused raw depth/MV guides remain explicitly approximate. No exact clean-guide producer is claimed. Product source, default RR→NR, frozen #338 and deployed candidate are unchanged.

## First actual failure and minimal correction

The v1 two-RR frame 0 returned `E_INVALIDARG` (`80070057`) from command-list Close before Submit. No new frame or readback executed. Baseline and raw-NR→one-RR controls had already completed 32 frames each. The actual v1 receipt contains 68 retired recordings: 66 controls plus the failed route's two successful Create recordings. It is not a completed 100-recording comparison.

The new resource helper issued a transition even when the observed state already equalled the target. After NR warmup, clean was already non-pixel read state 64; composition recorded an illegal 64→64 transition. v2 checks the actual observer state and omits only identical-state transitions. Explicit UAV barriers remain. An independent layout defect was also fixed: initialize all 13 typed-null SRVs + 8 typed-null UAVs required by the borrowed root layout, then overlay the four actual bindings. The layout defect is not asserted to be the first Close failure's cause.

Seven CPU controls derive from the actual old/new Move and descriptor layout source. The old frame-0 sequence fails the transition contract; the new sequence retains the three required transitions without a no-op. The ratio shader, child/NR/parent parameters, original caller flags, leases, exact queue/fence/Reset and all shared control sources are unchanged. Own-source diff and controls are in `raw_nr_probe_review/two-rr-approx-v2/`.

## v2 actual result

After Astra high independently checked 63 frozen files, root executed one new two-RR process, once, with 32 frames. Actual private child RR Create/Eval = 1/32; Feature18 Init/Create/Eval = 1/1/32; original parent RR Create/Eval = 1/32. All 34 new model/Create recordings retired with exact completion and successful Reset. No retry, concurrent development GPU test, game or production-route enablement occurred.

The comparison explicitly inherits v1's already completed 64 control frames and original binary identity. It does not pretend those controls ran the v2 binary. The comparison's 100 retirements are inherited 66 + new 34; v1's failed arm and two Create retirements remain separately preserved.

Frozen v2 manifest: `43ab6b6627fb8ab98d97787004e7b969b6d8ba4e74d1d725241abe5ba3fdc376`.

v2 executable: `52f21828f4eea2ce2496aef71b3321c5fbf72cd7ca43b28f46087f94db5e04ef`.

Actual v2 manifest: `978c8e81f713d3bb17b2f7e7dce4ead483a703b392af1148c387d9ff9b029a4e`.

Inherited v1 actual manifest: `97475bee24b6cf47828b0fc2f88e8074f868b6f6eba9f66c625d1468a5fe492f`; independent failed-run review manifest: `32630ebbb699c0ec938144913529095554202c8b910461ac96c0b1483971348e`.

Independent readback review found **zero accepted edited pixels**. Across all 32 frames, identity pixels = 1,927,728 and refused pixels = 27,563,472. In the 16 observed frames, 99.6766% were refused and no legal edit reached the parent. All composite pixels retained the original raw bits. Oracle agreement with this fallback is not an effective editing pass.

The observed input alpha is exactly 0.75 (FP16 `0x3a00`), while the new experimental shader incorrectly required raw alpha = 1. Frame 16 has positive clean RGB, nonnegative edited RGB and 908,477/921,600 pixels changed by NR, yet zero changed composite pixels. This proves an experimental alpha-contract mistake; it does not prove approximate raw guides or the two-RR idea ineffective. v1/v2 and their actual results remain frozen. Correcting the observed input contract requires its own CPU counterexample, packet and new validation; no production guard is changed to hide the issue.

This is flat-fixture inference/retirement validation, not effective two-RR editing, 3D quality, performance, actual-game acceptance or a new candidate deployment. Independent CPU readback/oracle/coverage review is recorded separately. Nonzero valid edit coverage and then a scene-based quality result must precede any claim of an advantage or flicker repair.

## v3: effective edit on the observed alpha contract

Astra approved only changing the experimental shader's editable alpha domain from the incorrect 1 to the observed 0.75, not arbitrary finite alpha. The complete host executable and all C++/INL sources are byte-identical to v2. The single shader predicate changed; no clamp, additional guard, product setting or lifecycle rule was added. Fourteen ratio/bit cases plus thirteen source-derived alpha cases passed, including an old-code-fails nonidentity 0.75 case. Identity/refused pixels retain the CopyResource base; unknown alpha and NaN payload still fall back unchanged.

Root ran one new 32-frame arm, once, after Astra's 55-file frozen packet check. Child RR / Feature18 NR / original RR each executed 32 times; all 34 new recordings retired. Each observed frame had both predicted and actual nonzero RGB changes. The result is `passed-bounded-twoRR-v3-effective-edit-with-pinned-v1-controls-functional-only`.

Independent CPU recomputation of all 288 new readbacks found observed-frame legal domain 99.563273%, identity 0.323364%, refusal 0.113363% at the boundary, and actual RGB changes in 14,517,331 pixels (98.451952%). All alpha u16 values and non-edit RGBA bits were preserved, no changed pixel lay outside the predicted legal domain, and legal values were within the previously approved one-FP16-ULP tolerance. The predicted and actual changed masks differ at 164,758 pixels (1.117337%); this is reported separately rather than claimed as an exact GPU write mask. Every observed final O differs from the inherited native baseline. This is effective editing evidence, not a quality benefit.

v3 frozen manifest: `ed149a17db683155226401033a859dc421b6c4e905bbd93a42c5dd6ec02d2746`.

Reused executable: `52f21828f4eea2ce2496aef71b3321c5fbf72cd7ca43b28f46087f94db5e04ef`; new shader: `11aa1705bd530075e7637acb074e6a00e1360e1007984fd746bdcfaff3e1a4b6`.

Actual v3 manifest: `c51d69e51f6be5c960b8d707acf9acf534fcb74ec715f029ea059184b5fc38cc`.

The next bounded gate uses the already successful 3D SceneFrame. Its separately observed alpha is 1 (`0x3c00`), so that route needs its own explicit producer contract, not the flat fixture's 0.75 predicate or a silent scene modification. No new product RR route or game candidate has been enabled.
