# beta0.4 Perfect Chain r3 experiment

Branch: `codex/beta0.4-perfect-chain`

This branch is an aggressive experimental successor to beta0.3.3-r3. It keeps the existing D3D12 Feature-1/RR safety model and adds a separate Mode-2 processing path designed around three goals: lower Feature 18 cost, stronger controllable neural edits, and safer HDR composition.

## New Mode-2 chain

```text
Game render-resolution colour + native depth/MV/exposure
  -> exact-area colour reduction to a stable NR working raster
  -> guide normalization/resampling
  -> Feature 18 at the independent working raster
  -> matched neural residual reconstructed at the full render raster
  -> HDR-headroom-preserving composition
  -> the game's original DLSS Super Resolution call
```

The game still receives a full render-resolution colour input. Only Feature 18 is allowed to work at a smaller stable bucket.

## Working-resolution modes

- Reference: Feature 18 follows the full render raster.
- Quality: output-relative quality bucket.
- Balanced: output-relative balanced bucket.
- Performance: output-relative performance bucket.
- Ultra Performance: strongest reduction, with a 50% linear lower bound relative to the current render raster.
- Custom: 50–100% of the current render raster.

Output-relative buckets are aligned and intentionally stable under ordinary DRS movement. Active guide extents remain a history-reset boundary, but they are no longer treated as a new allocation/storage identity by themselves.

## Multiplicative NR strength

`NR Effect Strength` is no longer limited to a 0–1 interpolation. The new compositor works on the model's log luminance ratio:

- `0`: true native-SR path; Feature 18 is bypassed by the existing zero-transfer fast path.
- `1`: follow the model normally.
- `2`: compound the model ratio twice.
- `4`: deliberately strong experimental result.
- `8`: extreme diagnostic ceiling; keep the ratio guard conservative.

The powered ratio remains bounded by `MaxLumaRatio`, finite-value checks, near-black confidence, edge confidence and the original-image fallback. Colour follow uses the same bounded log-ratio idea and can be raised independently.

## HDR composition changes

- Hybrid proxy is the recommended default: identity-like midtones with an unclipped highlight shoulder.
- Reversible proxy is available as an aggressive comparison mode and may flicker on bright moving lights.
- Matched residual compares the neural result with the proxy at the model raster, then applies that change to a proxy rebuilt from the full render-resolution source.
- HDR energy above the model proxy is preserved as headroom instead of multiplying the entire highlight by an unstable ratio.
- Negative/out-of-gamut results are moved toward a neutral axis rather than independently clipping RGB channels.
- `Original Lighting Lock` removes only a broad local model luminance component.
- `Edge / Hair Guard` reduces edits at high-contrast boundaries.
- A reset/camera-contract transition may suppress the visible correction for that frame while the native image is retained.

## Safety and fallback

The branch does not weaken the existing protections:

- F6 off remains a true native-SR fast path.
- Ray Reconstruction ownership remains a strict native pass-through.
- ShortFuse/RenoDX direct-NR ownership remains a double-processing bypass.
- Unknown formats, exposure layouts, resource bounds, command-list replay, queue migration or lifecycle uncertainty retain the game result.
- Legacy Mode 1 (`SR -> NR`) and the previous codec PSOs remain available for comparison.

## First validation pass

Test Reference at strength 1 before changing more than one control. Then compare:

1. Reference / 1.0 strength / Hybrid.
2. Quality / 1.0 strength / Hybrid.
3. Balanced / 1.0 strength / Hybrid.
4. Reference / 2.0 strength / Hybrid.
5. Reference / 4.0 strength / Hybrid.
6. Reference / 8.0 strength with MaxLumaRatio still at 4.
7. Reversible proxy only in a fixed bright-light scene.

For each run capture the displayed frame with `-`, request one raw-stage capture, and retain `nr-before-sr.log`. Required stress transitions are HDR off/on, DLSS quality changes, DRS movement, RR off/on/off, camera cuts and rapid indoor/outdoor exposure changes.

## Automated qualification

The branch runs three separate gates: structural invariants, compilation of every embedded Shader Model 5.0 entry point, and full English/Chinese DLL builds against a clean public ReShade checkout. Compiler output is retained in the branch so a failing gate is attributable rather than hidden behind a generic HRESULT.

This remains an experimental source candidate until those automated gates and live game tests both pass. A successful compile is not a claim of visual correctness or universal compatibility.

## r2 audit fixes

- Scaled bounded-R8 inputs remain in their verified value domain instead of receiving a second sRGB encode.
- Output-relative presets fail safely to the full render raster when the live SR output extent cannot be proven.
- Lightweight GPU delta diagnostics are sampled after changes and periodically rather than every frame.
- Lighting-lock and edge-guard composition reduce redundant full-resolution loads.
- Strength now reaches 8x, while the independently adjustable 1–16 maximum luminance ratio remains the hard safety ceiling.

## r3 final audit fixes

- Motion-vector scale and camera jitter are converted from the game's render-source pixel domain to the independent Feature 18 working raster. This keeps temporal displacements correct below 100% working resolution.
- Width/height alignment is selected as a pair, prioritizing the original render aspect ratio before small area differences.
- Physical guide reallocations caused by DRS reset history but no longer force a new Feature 18 context when formats, working extent and normalized flags are unchanged.
- Extreme SDR colour follow now compresses both negative and above-one channels toward an equal-luminance neutral axis instead of hard per-channel clipping. HDR positive headroom remains untouched.
