# #82 exact-size consumer preparation

Only prepared/compiled: **no executable launch or GPU run** in this slice.
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
