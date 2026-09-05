# FG/MFG control policy: first implementation slice

Relates to #16, #13 and #4. Base: beta0.4.1-r2 `175173f457f532a3d46800d0906047d3ae7f375d`.

## Implemented

Portable capability-gated decision logic and a change/acknowledgement gate. Follow game, Off, Fixed and Dynamic are distinct. Fixed multipliers are constrained by both the runtime generated-frame ceiling and the separately verified total-multiplier ceiling. Dynamic ignores the fixed generated count. No hard-coded Ada generation unlock is performed.

Unknown owners/inputs/pacing reject. Owned paths can suspend on transitions; unsafe sessions release only our control, never blindly switch off another component. F8 holds user changes but does not block an owned-path safety suspension. Restart-only and unsafe-boundary changes defer. Requests, accepted calls and observed runtime settings are separate. This does not prove actual display cadence.

`ChangeGate` is control-thread confined, not thread safe. Marshal UI changes onto a proven render/Present control boundary. Invalidate on rejected/unknown snapshots, ownership loss, backend/swapchain recreation and stale data; use a new capability generation before retry. A failed request does not retry every frame. Match acknowledgements to tokens and map backend revisions into our request/capability identity before calling Observe. Never synthesize an observed result from the request itself.

No runtime hooks, Streamline dispatches, external module loader or visible game menu are wired in this slice. `offlineAllowed`, timing and input fields are observer/profile evidence, not a detector implemented here. The 32x/1000 FPS policy bounds reject malformed observations; they do not advertise hardware support. Do not market this branch as an RTX40 MFG unlocker.

## Preferred RTX40 integration boundary

Reviewed upstream: `dashdogy/RTX40MFG-Unlock` at `4e776d068f91b4a665425542bb005dd57cc3d891` (V1.2 source).

- The core requires a game with an existing Streamline FG integration. It cannot add FG to arbitrary old games.
- `source/native/backend_bridge.h` exposes `MfgUnlockBackendQueryInterface` and exact `MFG_UNLOCK_BACKEND_ABI_V3 = 0x00030000`, with size/version checks.
- The snapshot in `reshade_bridge.h` separates desired/applied revisions, wrapper/provider generations, timing readiness, runtime maximum, safe maximum, dynamic support, rebuild requirement and frontend count.
- Implement an optional client of this public boundary before considering source-level patch merging. Do not copy the patcher's hook layer or load a second FG owner. Retain the existing early-load requirement; attaching a menu late is not proof of a valid FG pipeline.
- A future bridge must independently verify the pinned ABI/header layout and active runtime identities, map observed ceilings conservatively, retain module lifetime, prove FollowGame/Off semantics from the implementation and preserve original options on release. Unknown layouts fail closed. A settings counter is not proof that Ada generated frames are temporally correct.
- The repository declares MIT for its original code, but no upstream source or binaries are imported here. Any later copying must retain notices and separately review dependencies. NVIDIA runtime/model files remain excluded.

Normal NVIDIA FG, external Ada MFG, and hybrid FSR/Xe paths must have distinct labels. A minimum-runtime query alone is not enough to assert correct Ada timing. Current policy requires explicit ABI and timing evidence for ExternalAda before enablement; collect that evidence in an isolated upstream-only test before NR coexistence.

## Next runtime gates

First test a D3D12 title with no mod / NR only / external FG only / NR+FG. Verify NGX, swapchain and Present ownership, source-frame identity, Reflex, HUDless/UI inputs, loading/cuts and multiplier switches. Do not put NR on generated frames. Read `numFramesToGenerateMax`, dynamic support and runtime status from the active path. Capture actual display-change intervals, not just Present-call spacing or an FPS counter. Never chain two interpolators to imitate MFG.

## Tests

```
cmake -S tests/research/framegen -B build/research-framegen
cmake --build build/research-framegen --config Release
ctest --test-dir build/research-framegen -C Release --output-on-failure
```

GCC/CTest Release and Clang ASan/UBSan passed locally: 991 CHECK evaluations including ceiling sweeps. No Windows/NGX/game or Ada display-cadence validation was performed. Keep Draft and keep release integration disabled.

References:
- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md
- https://github.com/dashdogy/RTX40MFG-Unlock/blob/4e776d068f91b4a665425542bb005dd57cc3d891/source/native/backend_bridge.h
- https://github.com/dashdogy/RTX40MFG-Unlock/blob/4e776d068f91b4a665425542bb005dd57cc3d891/source/native/reshade_bridge.h
