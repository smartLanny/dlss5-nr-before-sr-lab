# Non-DLSS provider admission experiment

Relates to #4, #14, #10. Baseline: beta0.4.1-r2, commit `175173f457f532a3d46800d0906047d3ae7f375d`.

## Implemented, not implied

`src/research/temporal_provider.h` implements a portable metadata validator, a conservative FSR 3.1 observation normalizer, and R-to-N motion/jitter scale calculation. It is not included by the release add-on. It does not intercept FSR, create NGX features, upload textures, perform barriers, detect anti-cheat, or make any game compatible yet.

A passing metadata record is conditional on evidence supplied by a future runtime observer. Resource flags are attestations, not substitutes for D3D12 validation. `nativeFrame` means a real source frame rather than an interpolated frame, not proof of native guide quality. `Origin` must remain visible in diagnostics. Frame and adapter IDs are sanitized logical identities, never raw pointers.

## Initial supported metadata contract

D3D12, one source frame and adapter, known queue/lifetime, legal typed views, linear color, explicitly verified depth and current-to-previous motion conventions, valid R/O rectangles, explicit exposure and positive pre-exposure. NR work dimensions cannot exceed R. Negative motion scales are valid; NaN, zero scale, stale guides, generated frames, RR and conflicting owners reject. Master off performs no conversion.

The first FSR adapter intentionally rejects display-resolution motion, motion containing jitter, automatic exposure, unsupported versions and nonzero bases. These are unsupported adapter cases, not assertions that FSR itself lacks those capabilities. Keep the full native FSR call untouched on rejection. `apiMajor/apiMinor` identify the observed FSR upscaler contract, not the FFX API header ABI; a runtime shim must translate the SDK's actual type/version descriptors explicitly. Never cast this struct over an SDK pointer.

R-to-N mapping scales motion values and jitter exactly once. This helper does not resample MV/depth or prove the NR runtime's guide semantics. A future carrier must preserve actual resource formats, depth flags, exposure resource/value, optional masks and command-list lifetime in its GPU-owning structure; they are not fully represented by this metadata record.

## Runtime implementation order

1. Add an observe-only D3D12 FSR 3.1 shim using the exact public SDK and a pinned game/runtime profile. Prove resource state, adapter, frame/view identity, all create flags and ownership. Do not guess private ABI layouts.
2. NR off: implement a genuine DLSS SR carrier R-to-O, including exposure, reset, optional-mask policy and full parameter restoration. Identity/temporal validation precedes NR.
3. Feed the existing NR path and existing `NrGuideHistory` through a reviewed adapter; commit history only on successful evaluation. Do not replace the release history model with this validator.
4. Validate one FSR-only title, then a second contract. Only then extend to XeSS.
5. Old games: extracted UE DX11 TAA guides plus a D3D11/D3D12 transport first; engine profiles next; synthetic Feeder fallback last. Synthetic postprocess alone cannot lower true game rendering R.

## Test

```
cmake -S tests/research/temporal -B build/research-temporal
cmake --build build/research-temporal --config Release
ctest --test-dir build/research-temporal -C Release --output-on-failure
```

Current local validation: GCC C++17/Release/CTest and Clang with AddressSanitizer + UndefinedBehaviorSanitizer, all passing; 5,151 CHECK evaluations including dimension sweeps (not 5,151 independent scenarios). No Windows add-on build, WARP, NGX or real-game test was performed. DevSpace was unreachable; do not borrow prior release validation.

## Merge gates

No release-source changes in this slice. Before runtime integration: master-off/no GPU work; native Mode1/Mode2 parity; RR pass-through; unchanged fallback resources; context/lifetime stress over resolution changes; HDR/DRS/cuts; one FSR-only title with same-frame captures; actual per-stage GPU timing under #9. Passing these CPU tests is insufficient to enable a provider in release builds.

## Primary references

- AMD FSR 3.1 integration: https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/
- NVIDIA SR integration: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md
- Broad synthetic-input reference: https://github.com/jlrouzies-fr/DLSS5-Feeder

Independently written code; no third-party implementation, SDK header, runtime, model or binary is included.
