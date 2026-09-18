# D21 Perf5: Windows validation and owner test delivery

## Frozen build identity

Runtime/build source: `3e1dd7992a94dbdd4700fb41377a7a4f6f1bd102`.
Base: owner-tested Perf4 branch `ea9e0b896e6d386b719cddb916b66bc99c43768b`.
Version: `beta0.5-dline21-perf5`. Numeric PE version remains inherited 0.5.1.23;
identify this candidate by the full version string, source commit and hash.
This document is added after building and does not alter the frozen runtime.

| Artifact | SHA256 |
|---|---|
| Chinese Core | `eeb94db9aad222330e63b130e2b4ff40669fce88a5932b1a28656467e6ac1b14` |
| English Core | `3accbc96ad37398aec8a60b60cd24d1a26717e35aef1bfd4238d0ba62c7573a2` |
| Matched nrchain | `1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2` |
| Chinese Core-only ZIP | `9ad158328a679c22a81d17f5479422cea55a9910b43173e4aabbdca87fecec0a` |
| English Core-only ZIP | `86aacdd232684e431b3f65654bebaf15fe95f299e8f89cba73049dae31842530` |
| Chinese Core + nrchain OTA ZIP | `5e1dac77d48e0bc0c199aa3e123c6ed64f203b5778f7c68028909c28f6e1130d` |

OTA: `DLSS5-0.5-D21-Perf5-zh-CN-OTA.zip`, 1,645,945 bytes.
The delivery directory leaf is `d21-perf5-3e1dd799-20260918-ota`;
the owner handoff provides its complete machine path, not this source document.

The archive contains one Chinese Core, matched nrchain, instructions, licenses
and an `nr-branch-ota-v1` manifest. No INI, model, NVIDIA game DLL or unrelated
bridge is included. CRC, archive inventory, file sizes/hashes, embedded source
and version, x64 PE and companion exports were checked. The manager interface
was not exercised; nothing was installed in a game.

## Verification actually performed

The complete `build-perf5-candidate.ps1` finished successfully. Toolchain:
LLVM-MinGW 20260826/Clang 23.1.0, Windows PowerShell 5.1; fixed carrier,
ReShade and DirectXMath revisions were checked by the canonical builder.
The previously verified companion was reused only with the existing source/SDK
receipt checks; its binary hash is identical to Perf4.

- All 228 generated source-manifest files were independently rehashed.
- Both descriptor headers/runtime adapter match their committed source bytes.
  `nr_feedback_capture.inl` matches committed source PLUS the existing
  generator's exact `interop/nr_post_feedback.inl` include insertion. That
  expected generation transform was inspected; a raw source-tree byte match
  without this transform is not claimed.
- All five local gate receipts passed: original native SRW, Lock2 hotpath,
  Perf3, Perf4 and the new Perf5 tests.
- Actual frozen Perf4 dense-overlap negative control: 343,000 candidate
  predicates (budget rejected, exit 3). Actual Perf5: 3,426 (passed).
  These are deterministic test work counts, not FPS measurements.
- Candidate exact public-result differential, 64 address-union sequences,
  nested/pending/retirement tests, high-address/malformed input, 128 fragmented
  singleton ranges on overlapping one-byte-offset heaps, and ordered/raw
  fallback counter coverage passed. Timing builds include per-copy preparation.
- Existing randomized/concurrent descriptor and source-saturation tests were
  retained through the Perf3/4 gates, including 120,000 randomized operations
  and 8,000 threaded lifecycles.
- Descriptor CPU: 2,225 checks. Default registry size: 16,581,096 bytes.
- Canonical validation: all 46 verification groups passed; complete bilingual
  Core and carrier builds finished successfully.
- Entire generated Core descriptor WARP: 669 checks passed.
- Source transport WARP: 1,700 checks passed.
- Inherited state restore and snapshot readback/wrong-pixel negative gates
  executed. The final snapshot GPU readback and negative-control fields are true.
- All 26 resulting shader artifacts were rehashed and match their corresponding
  Perf4 artifacts. Exact-source/compiler cache reuse retains original receipts;
  this is NOT a shader algorithm change or a claim that cached shaders were
  freshly compiled.

## Explicit limits

The host lacks D3D12 Graphics Tools. The build used the explicit
`AllowMissingDebugLayer` switch, not a silent skip. Real WARP byte readback and
negative controls ran; `snapshot_debug_layer_verified=false`. InfoQueue/debug
layer coverage is not verified. Existing HLSL, unused-function and upstream
novtable warnings remain in build logs; compilation is not described as
warning-free. Hosted GitHub CI is reported separately from these local results.

New-candidate NVIDIA inference, game performance, weak-CPU benefit, long-run
stability and manager UI import have NOT been tested. No automatic installation,
release, stable merge or system/driver changes were made. First Feature18
creation latency was not changed or claimed fixed by this descriptor patch.

## Next owner test

Exit the game and keep the old Core/nrchain backup outside its search paths.
Use ONE Perf5 Core plus the matched companion. Preserve INI, ReShade, bridges,
models and identical graphics/NR/SR/RR/FG settings. Do not combine this with a
D13 small companion or another NR Core.

Only one new same-scene Perf5 OFF -> single-layer NR-to-SR ON -> OFF capture
and its F8 are needed for the next iteration; do not rerecord the original
four-version matrix. Verify NR success counts grow during ON and stop in OFF.
F8 now reports `start/end.descriptor.rangeQueries`: ordered/raw scope counts,
ordered range count, indexed invalidations, ordered and linear fallbacks.
These are cumulative operation counts, not CPU/GPU percentages. They confirm
whether the changed runtime input path actually executed.

Separate first-use initialization from steady-state windows. The owner's
observation that later warm F6 toggles are smooth remains recorded; this patch
does not claim every toggle stalls. Any persistent bypass, missing effect,
flicker or crash requires rollback and an F8/Core-log/version-hash bug report.
Safe bypass is not a performance pass. Keep #277/#272/#273 open until actual
candidate runtime evidence supports changing their status.
