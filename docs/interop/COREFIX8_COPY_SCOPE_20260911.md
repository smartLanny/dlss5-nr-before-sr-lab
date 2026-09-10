# Corefix.8: preserve untouched descriptors during large native copies

## Problem and behavior

[The Corefix.7 retest](COREFIX7_RETEST_COPY_20260911.md) has current native SR
calls but no new NR work, with missing typed Output evidence. The old production
copy observer reproduces an independent defect: exceeding its 256-descriptor
or 64-range observation limit invalidates the entire registry epoch. A stable
unrelated CPU Output UAV can then remain unknown until the application rewrites
it, even with ample CPU registry capacity.

The observer now retains the existing per-slot path for small copies. When it
cannot decode the copy, it records the explicit CPU destination address ranges
for the duration of the native call. Those ranges are invalidated on entry and
exit and remain unavailable while the copy is in flight. Reads, current-receipt
checks, and overlapping publication reject masked destinations. Unrelated slots,
including slots in the same heap, preserve their identity and value.

The copy scope owns no D3D object, holds no registry lock across the original
API, and forwards that API once with unchanged arguments. Its range array grows
with the number of destination ranges rather than the number of descriptors.
Untrustworthy range arithmetic or allocation failure uses a global scope that
also blocks publication throughout the native call. Old writes cannot restore
invalidated values after either scope ends. Multiple scopes may overlap and
finish in either order; raw CPU ranges also cover newly tracked slots and heap
address reuse while a scope is active.

Large-copy destinations remain unknown. The change does not infer their format,
raise registry capacities, decode shader use, or introduce game-specific rules.
The existing current-call NativeClear CPU gate and known GPU conflict checks
remain required. CPU declarations do not become GPU-use or submission proof.
F8 adds cumulative `scopedCopies` and `globalCopies` counts to the registry row.

## Identity and validation

Candidate identity is `0.4.7beta-corefix.8`, numeric Core PE `0.4.7.13`.
Frozen source, actual artifacts, and same-source validation will be recorded
after the candidate gate completes. Component development checks do not certify
the frozen candidate, NVIDIA NR, or game visuals.

Development history is preserved. The first WARP build stopped on fixture
handle/bool conversions. The second executed and caught an actual integration
gap: clearing a slot value without retiring its observation produced
`ViewUnknown`, which the existing CPU-only NativeClear gate correctly rejected.
The fix retires only the touched slot's epoch; unrelated epochs remain valid.
An intermediate run inverted that expected gate result and is diagnostic only,
not acceptance of the intended behavior. The final component run restores the
positive current-call requirement and passes 668 WARP checks. CPU registry
checks pass 1709 assertions; synthetic-COM production TU checks pass 230, with
six negative controls. Earlier synthetic fixture placement failures are also
retained and are not counted as passes.

The regression coverage includes the actual production registry, full Core
translation unit through synthetic COM, and real native D3D12 WARP copies and
Clear readback. The old global-copy policy is also a negative control, required
to fail the unrelated-view preservation oracle. Tests preserve rejection of
unknown destinations, wrong resources, late writes, overlapping scopes, and
untrusted global fallbacks.

## Owner handoff

Use the confirmed Manager external-runtime deployment entry, preserve settings
and dependencies, and replace only the backed-up language Core where possible.
The next game check is one ZZZ POST scene for about 30 seconds, one F8 report,
and whether sustained effect and flicker changed. See the [Chinese guide](../testing/CORE_047BETA_COREFIX8_ACCEPTANCE.zh-CN.md)
or [English guide](../testing/CORE_047BETA_COREFIX8_ACCEPTANCE.en.md).

The session's exact 15 invalidating calls are not identified by the old report.
The source defect is proven; final visual repair remains unverified. Death
Stranding is unchanged and still unresolved. Issues #2, #205, and #203 remain
open. This candidate does not imply merging into main, tagging, or a stable
release.
