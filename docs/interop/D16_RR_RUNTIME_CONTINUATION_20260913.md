# #258 continuation: D3D12 recording backend; still not game-ready

Continues 34d2b9f on D16 3c13f69. No active D16, game installation or settings changes.

## Actual new implementation
`nr_edit_dx12_dispatch.cpp` creates real D3D12 PSOs/root signature, twelve immutable descriptor/256-byte constant slots per stage, pins resources and retires only on actual queue/fence completion. No hidden Execute/Signal/Wait.
`nr_rr_edit_program.cpp` binds PrepareGuidesCS/AccumulateCS before RR and ComposeCS after the matching successful native RR. Transitions are only for its explicitly owned UAV outputs; native resource states and complete state restoration stay in Core. Pairing, source/generation and actual queue cookies are retained.

## Validation actually run
GCC strict and Clang ASan/UBSan executed the production dispatch-slot controller, including 10,000 slot reuses, stale/duplicate receipts, queue/fence mismatch, incomplete work and poison. The Windows TUs and shader dispatch were NOT compiled/run here. ROG was unavailable. No NVIDIA/game/performance claim.

Local Windows continuation:
```
python scripts/validate-d16-runtime-backends.py --mode rr --cxx <LLVM-MinGW-clang++> --windows
```
This compiles the Windows files and runs WARP PSO/descriptor/constant construction, NOT shader GPU dispatch or game injection. Then run existing validate-d16-edit-experiment.py and actual GPU readback tests.

## Still required in D16
1. Extract the private-image calculation from PrepareBeforeSr into a callable returning E_R WITHOUT game Color substitution, fake feature_id or shared g_config mutation. Preserve real NR1->NR2, N1/N2 and current composition.
2. Add a default-off Feature13 experimental handler with actual source/ownership guards. Original RR exactly once with untouched input.
3. Allocate/pin zero-origin single-mip canonical R resources: RGBA32F reprojection/edit, R32F depth/history. Old/new history cannot alias. Private final is O. Caller holds native inputs and all histories through every consumer fence.
4. Establish exact SRV/UAV states and wrap WHOLE work in current command-state scopes. RecordRrBefore may fail after stage1; inspect and retain its tickets rather than assuming false means no commands.
5. Call RecordRrAfter only on exact same source/key/list and successful native RR. Apply existing final HDR/headroom policy then legal copyback. SubmitRr needs actual queue AND its Core identity cookie, not pointer-as-cookie.
6. Add GUI/INI only when this executes; off must add no allocation/GPU work. Context resources/list allocators remain Core-owned. Failure/resize/draining retain all recorded dependencies.

Until these steps, this is a real backend implementation but not an enabled RR feature. At equal NR N it is not automatically faster than existing reduced post-NR. Compare clean RR, full post-NR, same-N reduced post and carry. Pre-RR colour is noisy; depth rejection does not recreate clean post-RR inputs. No quality or FPS advantage is claimed.
