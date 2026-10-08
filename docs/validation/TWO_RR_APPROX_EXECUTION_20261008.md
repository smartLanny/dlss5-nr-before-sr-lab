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

This is flat-fixture functional validation, not 3D quality, performance, actual-game acceptance or a new candidate deployment. Independent CPU readback/oracle/coverage review is recorded separately. A scene-based quality result must precede any claim of an advantage or flicker repair.
