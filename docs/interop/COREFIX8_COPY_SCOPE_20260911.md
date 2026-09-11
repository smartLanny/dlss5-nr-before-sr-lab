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
Frozen validation source is `69490510345f5a34481e7a7600defc87e7df62f0`. Its production source is identical
to runtime freeze `b6ce55ef847ff18892798304e788fc4e2fe0ba71`; the intervening
commit changes only two validation scripts.

The final same-source CPU, bilingual compile/link, and WARP run passes all
82 required checks. The staged registry passes 1709 assertions; the full
production TU passes 230 and detects all six negative controls. Descriptor
WARP passes 668 checks, including real 257-descriptor and 65-range copies,
preserved unrelated CPU Output evidence, current-call admission, continued
GPU-proof rejection, and Clear texture readback.

Source manifest SHA-256: `57b6a5af7998a693a580254ec8c5d2aed8c9059e7a3b1f9d2fa51ed297524a48`.
Validation JSON SHA-256: `3a3b37993c132ead1c1a081ca48656a3c03a51976c4daedf2d985272ffcd131a`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `DLSS5-AI-Rendering-Upscaling-0.4.7beta-corefix.8-@野生的装机宅-Bilibili.en.addon64` | 3213824 | `4a52a62eeb2dc9e6d32a94c0cb9f0df26203582612ec72bf55bc921b4dcbd478` |
| `DLSS5-AI渲染超分版-0.4.7beta-corefix.8-@野生的装机宅-Bilibili.addon64` | 3241472 | `504bb48b1137b02a9ab126fe10337f0331a93684c413f411f4fea4fa47eb1b8d` |
| `nrchain_nvngx.dll` | 8192 | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

The first full run remains failed: its 27-scene post-output WARP case exceeded
the existing 30-second process limit. The same current executable subsequently
passed 5977 checks in about 30.3 seconds; the previous frozen candidate passed
the same case in about 29.6 seconds. Only that compound case now has a bounded
90-second limit, with elapsed time recorded; other cases retain 30 seconds.
The stale numeric PE assertion was also replaced by comparison against the
source manifest. Production code was unchanged for the successful second run.
Its compound post-output case completed in 22.8 seconds.

These results do not certify NVIDIA NR, game visuals, performance, a newly
tested menu, or D3D12 debug-layer validation. No game files were replaced.

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
