# #82 exact-size consumer preparation

Preparation was followed by one parent-reviewed fixed hardware batch; see results below.
The four allowed dimensions, in order, are 320x184 (control), 320x176, 256x144,
160x88. No other dimensions or product percentage range are admitted. This is
one Feature 18, one parameter object, one frame, Reset=1, one process per case.

Base is the successful consumer source `dda140521c92aa53eb03092740d528a0f3c54e97`.
The generator extracts its exact Module/NGX/Bind/Barrier helpers and substitutes
only explicit work width/height in Bind. It retains the official MSVC 17-slot
parameter interface, real `_nvngx` allocation and NRChain forwarding. After Bind,
the fixture explicitly sets SDR create flags=0. It does not copy Core orchestration
or change production algorithms, Manager, the earlier cascade or SyntheticMask trees.

All cases start with the same deterministic RGBA16F 640x360 SDR ramp/checker source.
Reduction uses exact nonnegative integer overlap areas: source/destination cell
boundaries share units of N.width/N.height; per-output sums of x/y weights are
640/360, and normalization is 640*360. There is no negative filter, center shift,
zero-padding or invented threshold. 320x184 and 160x88 have different aspect ratios;
the whole source rectangle is mapped to each N rectangle explicitly (no crop).
Depth is constant ordinary 0.5 at N; motion is zero at N; exposure=1; jitter is
known zero for this static synthetic fixture. NR is 1:1 N→N at each listed width/height.

Each Create and Evaluate completes a real serial same-queue fence. Dynamic
readback uses GetCopyableFootprints, strips row padding and saves RGBA16F little
endian bits at the actual width/height. It does not use the older fixed-1280
ReadOutput routine. Files: source-640x360.rgba16f, input.rgba16f, output.rgba16f,
input-after.rgba16f. SHA256, component finite counts, alpha minimum/maximum and
final fence value are printed; input-after must match input exactly. Nonfinite
output fails. Alpha change is reported, not assumed impossible or hidden. No
minimum output change is required and no quality inference follows from changes.

NGX errors, allocation failures, D3D12 errors, opaque partial recording or timeout
end the process without cleanup/retry. Successful feature release and parameter
destruction occur only after all fences. The fixed batch runner refuses an old
evidence directory, launches sequential processes and stops the entire batch on
the first nonzero exit or timeout. Remaining cases then remain untested.

Full MSVC14.44 x64 compile/link succeeded with `/EHsc /O2 /MD /utf-8 /std:c++20`,
the same fixed SDK a291cc7d2cc642a51566f3dfd5376f635cd1b284 headers/static library
as the successful consumer, existing FidelityFX headers for reused helpers,
and Windows D3D12/DXGI/BCrypt libraries. No new dependencies were downloaded.

Generate into a new private directory:
`python tests/nr_size/prepare.py <new-output-dir>`.
The generator records source inputs. The executable's fixed CLI is:

```text
fixed_size_gpu.exe --fixed-size 320x184 <bootstrap-dir> <nr-bundle-dir> <new-case-output-dir> SyntheticAreaReset1
```

`run-fixed-batch.ps1` additionally requires `-AuthorizedRun`; it is supplied for
parent review, not permission to run now. The actual loaded factory path is
printed; runtime/module hashes and all raw-plane hashes must be locked for the
eventual authorized batch. The signed model's minimum dimensions remain unknown.
Compile success does not establish any of these four Create/Evaluate admissions,
image quality, temporal validity or permission to expose 1–49% in the product.

Prepared artifact SHA256 locks (private binaries/logs, not committed):

| Artifact | SHA256 |
| --- | --- |
| generated fixed_size_gpu.cpp | `5e32087b4e279beba92413fbe0cce846390bfee03bc4fbef4ed5a5f8391c5389` |
| fixed_size_gpu.exe | `21c0ade929ec0fc4c25a40fca162887625a64135da4b387669b3938b10db10fe` |
| build.log | `0f460a5d37f974a91b1dd6619b496c81dc7b75a5a6b7a1eb95d8e91d54f52801` |

## Single authorized hardware batch, 2026-09-08

Frozen host source `0fb7064f9569f9687c5152413aab205551f804a6` and the executable
above ran exactly once per case, in the documented order. All four processes
exited 0 without timeout; Create/Evaluate/Release and parameter lifecycle returned
0x1, final completed fence=5 for each. No retries or omitted cases. GPU was released
before the independent CPU check. This admits only these exact four dimensions
in this synthetic SDR/reset fixture; the model's general minimum remains unknown.

The standalone CPU verifier decoded actual binary16 with Python `struct('<e')`,
recomputed all 16 raw-plane SHA256 receipts, checked exact byte sizes, confirmed
input-after equals input, and confirmed all four source planes are identical.
Every plane is finite. All four output alpha ranges are exactly [1,1].

| N | Output components, all finite | Output SHA256 |
| --- | --- | --- |
| 320x184 | 235520 | `6a1a4eb63fa7fc02952b1dfbbccd514e1fd91bfae67a2c7aab19389521e3d530` |
| 320x176 | 225280 | `e63253ab35f4b7eb7d1c07e0351125a4cc7f82194fad399f094680cb13cbb676` |
| 256x144 | 147456 | `79d2bbb3c3c4f07dbe63b14f7c7635dd47c58a981d55c147fe92ffb2b4f8df74` |
| 160x88 | 56320 | `4e0b2ca97b7b3f11b3be388676354f073d54d58fda5e1e80f7adf819aafce85f` |

Actual loaded factory/NR/bridge module paths were captured per process. SHA256:
factory `1496cd88ff70d1a8f9bd83fdb4d9d6dc58709a963a68f6b73bba7c7b607c6188`,
NR `6eb209e764f39872625debd6abaf45e2bb6322f6f270f781f70c059ae30b3927`,
bridge `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb`.
Available bootstrap SR DLL was `be6e434a94ca32499515eb62ca0e6c274526055d568d0426e4c652dcdfb6ee6e`.
Pre/post executable and runtime locks matched. Complete stdout/stderr/process
receipts, raw planes and CPU report remain private. CPU report SHA256:
`7cf878fe76e8f083b3c60e9313d148d19030020fb5dfe66ebf7de831cf5cd55f`.
The verifier itself performs no GPU calls. No quality or 1–49% product claim is made.
