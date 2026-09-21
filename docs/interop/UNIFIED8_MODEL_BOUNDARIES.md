# Unified8 single-model boundary audit

No model-DLL A/B, unknown Preset scan, runtime callback integration or model
default change is part of this audit. Temporal production files remain frozen.

## Fixed runtime identity

The locally applicable NR runtime was checked again on 2026-09-22:

- File version: **310.8.0.0**, size **165,840,496 bytes**.
- SHA256: `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e`.
- Authenticode: **Valid**, signer NVIDIA Corporation.

The private runtime is not redistributed by this source change. A version label
alone is insufficient; final tests must bind the exact DLL, Core source, bridge,
compiled objects and shader receipts they actually use.

## What a live setting change actually does

`CurrentNrTuningSignature`, `SameNrTuningSignature` and `FindOrCreateContext`
make intensity, local tone, local structure, effective skin strength, AutoMask,
style and UI correction part of the model's immutable creation identity. Jitter
mode also changes this identity. Input-filter selection participates only in
the pre-SR context; changing it does not recreate an unaffected post-SR model.
When AutoMask is disabled, the effective model skin value is -1; the saved skin
preference is retained separately.

Core therefore implements **live UI change → safe retirement → new Create**,
not undocumented hot mutation of an existing opaque model. The new instance
uses the latest complete setting signature. A ready old instance is released
outside the chain lock before replacement allocation; pending GPU dependencies
remain protected by their own fences and recording leases. Waiting for release
does not change the user's selected native processing route.

| Boundary | Existing production behavior | Evidence limit |
| --- | --- | --- |
| Model tuning above | Retire and recreate | Same parameter signature does not prove identical generated pixels |
| Work dimensions / guide storage / normalized Create flags | Retire and recreate | Real 4K Create suite has no Evaluate or output comparison |
| Equivalent rotating guide resources | Semantic identity, not allocation pointer | Must retain the actual per-frame guide lifetime/state contract |
| Guide mapping, scale, region, projection | Reset history; commit mapping only on successful Evaluate | CPU history proof is not model motion-quality acceptance |
| Scalar exposure / exposure-source contract | Codec canonicalization; contract changes or a >0.5 EV jump request Reset; delayed texture evidence has its own Reset path | Color-only real NGX cases use unity exposure; no complete HDR hot/cold output matrix |
| Parameter type | Float for four strengths; signed AutoMask; unsigned Style/UI correction | Runtime 310.8 accepts int/uint getter cross-reads; the explicit unsigned writer is not evidence of an unsigned-only vendor contract |

## Existing executable coverage

| Facility | What it actually establishes | What it does not establish |
| --- | --- | --- |
| `tests/compat_present_ngx_host.cpp` / `scripts/validate-compat-present-ngx.py` | Signed Feature18 cold bootstrap/Create/Evaluate/Release; one through five independent layers; common work scale; typed depth=1/MV=0 payloads; bound per-layer tuning; actual P/N and plane-to-plane chaining; OFF | Native game guides, non-unity exposure, live same-instance parameter mutation, hot/cold output equality |
| `tests/external_provider_ngx_host.cpp` | Actual external owner lifecycle and one/two signed model instances; pending release cannot destroy resources; duplicate/OFF behavior | Provider delivery in a game; arbitrary guide/exposure equivalence; hot/cold model output equality |
| `tests/post_scale_create_ngx_host.cpp` | Actual fixed-flag Create/Bind/Release at 3840×2160, 3776×2120, 2720×1528 with serial/overlap memory evidence | It explicitly does **not** Evaluate; zero color/MV and depth=1 cannot establish picture equivalence |
| `tools/nr_model_lab.cpp` / `scripts/run-nr-replay.py` | Independent real inference; complete compatible 16-frame `.nrf` replay with runtime identity and actual output comparison; bounded historical 1→2→1 structure experiments | Old cropped/zero-guide experiment is not current Core/game acceptance; private runtime callback mode is not a Core feature |
| `scripts/validate-model-boundary-cpu.py` | Actual extracted Core admission/signature/retirement functions, 15 cases; nine creation-identity changes, unaffected post input filter, guide Reset commit, three storage/flag changes, work-size change | No GPU, opaque Create or model output equivalence |

The earlier D9 note reports bounded cropped-proxy cold recreation/runtime
callback output comparisons. Preserve it as historical evidence. Repeated input
still showed temporal drift; it is not a universal convergence or all-settings
equivalence claim. The known Preset-zero control remains historical, without a
new scan or default recommendation.

## This audit's new checks

The 15-case CPU test passed against the current actual admission functions. It
checks that old pending models retain their original signatures, the replacement
signature equals the corresponding cold-admission signature, failed Evaluate
does not consume a guide Reset, and release deferral retains the selected route.
The existing UICorrection unsigned-writer test also passed. The first fixture
iteration incorrectly expected the pre-only input filter to recreate a post
model; the corrected test asserts this intended non-effect and separately tests
the pre context. No production change was needed.

## Smallest remaining real-model experiment

Use this one fixed signed DLL, one explicitly selected model setting change,
one complete compatible captured P/depth/MV sequence and the same work raster.
Record actual typed parameters immediately before every Create/Evaluate, actual
instance identity, Reset and exact completion. Compare three ordinary Core
sequences: cold A, live A→B→A through existing retirement/recreation, cold B.
After aligning Reset/history and source-frame index, compare complete FP16 N
planes and bounded image regions, retaining repeated A runs as the drift
reference. A parameter-block match is a prerequisite, not an output verdict.

Do not register the experimental runtime callback in Core to make this test
pass. If the source sequence, guide storage or exposure contract cannot be
replayed exactly, mark that route unverified. Native HDR and non-unity exposure
need their own complete source/guide/Reset sequence; color-only placeholders
cannot stand in for it. No additional GPU experiment was run in this audit.
