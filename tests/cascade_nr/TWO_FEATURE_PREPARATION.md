# #113 isolated two-instance consumer preparation

Built, **not executed**. This narrows the earlier ledger assessment: absence of
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

## Compile receipt

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
in the owner's evidence directory. No process launch, GPU or NGX test has run.

After owner review and an exclusive GPU window, the single proposed invocation is:

```text
two_feature_gpu.exe --synthetic-static-two-feature <absolute NGX runtime dir> <absolute NR bundle dir>
```

Before that invocation, lock the actual `_nvngx`, signed NR and bridge module
hashes alongside the executable and preserve its complete output, including any
failure. The executable needs the actual official runtime installation; compile
success does not prove runtime initialization, model support or finite output.
