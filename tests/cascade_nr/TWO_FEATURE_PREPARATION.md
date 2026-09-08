# #113 isolated two-instance consumer preparation

Built and executed once after parent source review; results below. This narrows the earlier ledger assessment: absence of
a production Core NR-only seam does not prevent an isolated consumer from using
the existing `NRChain_*` bridge and official parameter factory. This host does not
load Core, invent view IDs, call External Submit, or repeat `PrepareBeforeSr`.

The test uses two simultaneously live features and independent parameters from
the loaded official `_nvngx.dll` factory. It calls signed Feature 18 through the
existing bridge. MSVC compiles the official 17-slot `NVSDK_NGX_Parameter` class;
there is no guessed MinGW C++ layout or locally implemented parameter block.
Input/output/reset/width are read back through native Get slots before Create.

One static synthetic 1280x720 frame has a bounded linear RGB ramp, constant depth
0.5, zero motion and exposure 1. No camera/game inputs are claimed. P0, A and B
are distinct textures. Each Create and Evaluate is followed by a real serial
queue fence. Layer 0 reads P0 and writes A; A is read back and transitioned to
shader-read before layer 1 reads A and writes B. P0's before/after readback hash
must match. Both outputs must contain exactly 1280x720x4 finite FP16 values.

Every layer gets Reset=1. Zero jitter describes the source fixture only;
layer 1 is explicitly `phase=unverified` and `DLSSNR.Jitter.Valid=0`. Static/reset
does not establish the signed model's downstream phase or guide contract. The
test performs no composition or transfer; any future composition must compare
only the final successful output against immutable P0 once. Finite output and
successful calls do not establish enhancement, image quality or temporal safety.

All non-success NGX results, mismatched handles/parameters, failed D3D12 calls,
timeouts or nonfinite output immediately exit the process without recovery calls
or feature cleanup. Even a failed Create returning a handle is retained until
process termination. Success releases both features only after completed work,
destroys parameters, shuts down the bootstrap, then terminates the isolated host.
Signed runtime/module dependencies remain process-owned. No runtime matrix,
fallback provider, SR, FG, or production activation is included.

`direct_sr_gpu.cpp` helpers are reused by including its translation unit with its
entry point renamed, matching the existing private parameter probe pattern.
`NR_CASCADE_FAIL_CLOSED` only changes the helper Check failure path for this test;
the ordinary SR test's behavior is unchanged. Its unused SR entry is never called.
The FP16 helper approximates subnormal values, so only finiteness and raw-bit hash
checks are used here, not numeric accuracy claims.

## Initial compile receipt (before runtime authorization)

Source base `abc2952b60fb6a11effc4c3e85dae07a30912782`; separate branch
`codex/cascade-consumer-20260908`. MSVC 14.44 / Windows SDK 26100, x64,
`/EHsc /O2 /MD /utf-8 /std:c++20`, full compile and link succeeded. Dependencies
reuse FidelityFX 1.1.4 API headers needed by the included SR helper and NVIDIA
DLSS SDK commit `a291cc7d2cc642a51566f3dfd5376f635cd1b284` headers plus matching
`nvsdk_ngx_d.lib`. No new third-party source or binary is committed.

| Input/output | SHA256 |
| --- | --- |
| official nvsdk_ngx_params.h | `51a1f94116cf82f95c63a2c197371ed22e08bddb09918d36cc6e2ac712753c80` |
| official nvsdk_ngx_d.lib | `31e82b4ec3242ec6e5b42c73e1f5f5e260338a6ee213bd64444f6c2fa364aa84` |
| two_feature_gpu.exe | `b988713c5cee3969a0a8600bf74b2eac66b656bdd08ba150e1cd086e1fdc23e7` |
| build-final.log | `cdadd39b9b1ae7858513fd618bf1400164d99a4589838f9ff208936e535c5dfa` |

Link libraries: matching NGX static library, d3d12, dxgi, dxguid, version, setupapi,
shlwapi, ole32, advapi32, user32. Private build command and artifact are retained
in the owner's evidence directory. At this initial preparation stage no process,
GPU or NGX test had run.

The single invocation reviewed by the parent task was:

```text
two_feature_gpu.exe --synthetic-static-two-feature <absolute NGX runtime dir> <absolute NR bundle dir>
```

## Authorized single hardware run

On 2026-09-08, after parent review, source
`dda140521c92aa53eb03092740d528a0f3c54e97` added loaded factory path, P0 initial/final
hash and layer input/output hash receipts, then was rebuilt. The sole invocation
used the prior successful bootstrap environment's DLLs in a separate directory.
Exit code 0, no timeout, empty stderr; PID 82740 exited and GPU was released.
No retries, parameter sweep, game, Core production integration or composition ran.

Both independent native parameter allocations and readbacks, signed Create and
Evaluate calls, releases and parameter destruction returned 0x1. The two layer
readbacks completed queue fence values 6 and 10; each contained 3,686,400 finite
FP16 components and zero nonfinite components. All Create/Evaluate stages also
completed their own preceding fences. The table uses the helper's raw FP16-bit
rolling hash, **not SHA256**:

| Surface | Hash | Relative to input |
| --- | --- | --- |
| P0 before and after both layers | 15496935732297439107 | unchanged |
| A, layer 0 output | 17947891075543067856 | changed from P0 |
| B, layer 1 output | 5602928370776414540 | changed from A |

This proves two model instances executed/completed and produced finite output in
this one reset-only synthetic case. Hash changes are not a quality improvement
metric. Layer 1 phase remains unverified; no general temporal cascade contract or
production enablement follows. Raw image planes were not saved by the reused
readback helper; complete stdout/stderr, process receipt and module/hash manifest
are retained privately. No pixel-inspection claim is made.

Final lock (all six pre-run module/executable hashes matched after the run):

| Artifact | SHA256 |
| --- | --- |
| two_feature_gpu.cpp | `55cf754a7f0524e4a191bcf06292d010931af89dfdf6bcfb1c16bc6c64aea95e` |
| actual host executable | `2451b31bb5fa17e1d360565a054f0f2ea1b5f8c808e9c358a366c25ff4a83797` |
| actual loaded _nvngx.dll | `1496cd88ff70d1a8f9bd83fdb4d9d6dc58709a963a68f6b73bba7c7b607c6188` |
| bootstrap nvngx.dll | `1bf72519efb109ff5495b362598fde98f3cda6628a7ba28f8f9e9d9862edffc6` |
| available nvngx_dlss.dll | `be6e434a94ca32499515eb62ca0e6c274526055d568d0426e4c652dcdfb6ee6e` |
| actual loaded nvngx_dlssnr.dll | `6eb209e764f39872625debd6abaf45e2bb6322f6f270f781f70c059ae30b3927` |
| actual loaded nrchain_nvngx.dll | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |
| stdout.txt | `cb7f2aed7d090723b52a80d10a0c8c7a10ae22712e47b2277fc9b813a022f020` |
| validation.json | `6212af3981e3ddb0f1de662a9a3eb89348cabc5d003885b9e067b36563ea1fab` |

The parent task independently rehashed all eight recorded artifacts and all six
module/executable prelocks after the run, and matched the source, executable,
stdout and validation identities above. The later integration/documentation
commit is not a new GPU build. No host process remains.

## Next decision boundary

The two-instance runtime and failure boundary is now demonstrated for this exact
synthetic reset case. A production 2–3 layer implementation still needs a proven
downstream sampling/guide/history contract. It must not infer valid zero jitter
from the finite reset result or treat the last completed image as safe recovery
after an opaque recording failure. The independent CPU ledger remains in the
0.5 candidate; its fake-provider cases are a separate evidence class. No cascade
control is exposed in Core or Manager by this test-only change.
