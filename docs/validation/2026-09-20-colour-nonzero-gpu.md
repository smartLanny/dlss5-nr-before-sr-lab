# Nonzero colour recovery: actual FP16 production consumers

## Result and boundary

Generated source revision: `0ff39bfaac58e4be4b838e48db7f61ef312ee794`.
All 244 generated-manifest files were hash-verified. The extracted production
HLSL hash is `7ce6fdf239392a7baaed2d4c8a23f66d213c12d9ce179e47d780ba1c0e76e2e5`.
The runner verifies exact HLSL bytes and per-entry bytecode receipts before
reusing the release O3 `EncodeV2CS`, `CombineV2CS` and `CopyCS` artifacts.

- NVIDIA GeForce RTX 5090 D (`10de:2b87`, LUID `00000000:0001000f`):
  **360 cases PASS**; deliberately wrong source origin fails the encode oracle.
  Hardware selection rejects software adapters and has no WARP fallback.
- WARP: **new nonzero matrix FAIL**. At flat linear SDR, 100%, positive
  brightness, raw RGB outputs the original `0.25`, despite actual decoded
  `P=0.249516`, `N=0.272859`; the paired oracle expects `0.273342`.
  Legacy and protected composition in this case output `0.273193`.
  The failure remains a strict gate, with diagnostic S/P/N/E values. Its cause
  is unresolved; the adapter difference alone does not establish a driver bug.
- The inherited WARP post test separately passes its 48 scenarios and
  LightingLock/colour-budget checks with the corrected FP16 fixture.
- Production shader/defaults are unchanged. No NVIDIA Feature 18 inference,
  native SR output O, game run or temporal quality is verified here. N is a
  controlled nonzero texture derived from actual GPU-read P, uploaded and read
  back as FP16. These results do not resolve the owner's 99/100 game regression.

The private run directory retains stdout/stderr and a receipt containing source,
template, executable, bytecode and log hashes, actual adapter identity and each
gate result. A failed gate writes a failed receipt and returns nonzero.

## Fixture correction

The old CPU float-to-half helper combined a rounded mantissa with the exponent
using bitwise OR. A mantissa carry could disappear: `1.9998` became `1.0`,
`0.49997` became `0.25`. The helper now performs IEEE nearest-even rounding with
correct exponent carry, subnormals and nonfinite values. Readback now recognizes
half infinity/NaN instead of presenting them as finite values.

CPU upload conversion and GPU typed storage are tested separately. Actual
production `CopyCS` writes FP32 boundary values into FP16 and verifies truncation
towards zero and finite overflow saturation, as specified by
[Direct3D floating-point format conversion, section 3.2.2](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
This is different from the controlled CPU upload's nearest-even policy.

## Matrix and checks

Six inputs (flat, grayscale step, colour step, saturated step, HDR step and sRGB
step), five edits (identity, positive/negative brightness, positive/negative
RGB colour vector), four scales and three consumers give 360 cases.
Consumers are actual default D21 (independent-chroma bit clear), protected RGB
and raw RGB. No N2, edge recovery or proxy-first option is enabled.

The active source is 257x129 with nonzero source/destination origins and a
slanted step. Production alignment selects 257x129 / 256x128 / 192x96 / 128x64
for 100/99/75/50. An even 256x128 fixture would incorrectly make 99 and 100 use
the same raster.

Checks cover exact-area preparation from actual FP16 S, actual nonzero N-P,
finite output, alpha preservation, untouched destination borders, brightness
direction and flat-field scale consistency. Raw RGB checks the independent
paired-grid equation `E = S + (N-P) * sourcePeak/preparedPeak` on in-gamut,
unclamped pixels. It requires over half the image to satisfy that domain;
out-of-domain pixels still receive finite/alpha/border checks.

On NVIDIA the maximum preparation error is `0.00048828125`. Maximum absolute
raw-oracle error is `0.00718403` on HDR, within the explicit magnitude-relative
FP16 tolerance. Step images intentionally need not match across resolutions:
the log records their differences rather than accepting them as evidence of a
bug or judging their appearance. The largest 99/100 stored-channel difference
is `0.0546875` (legacy HDR brightness+) and `0.015625` (RGB HDR brightness-).
These controlled, small-image measurements are not game visual-quality scores.

## Reproduce

```powershell
python scripts/validate-colour-nonzero-gpu.py `
  --source-root <generated-core> --toolchain <llvm-mingw-root> `
  --codec-artifacts <verified-codec-artifact-directory> `
  --output <new-private-run-directory> --hardware
```

Omit `--hardware` and add `--include-inherited` for WARP plus the original post
regression. Omitting the artifact directory compiles the exact extracted HLSL
at the original O3 level; it does not substitute a simplified shader.
