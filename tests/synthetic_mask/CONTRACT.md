# #162 SyntheticMask seam experiment

Investigation at `92f2f216ed82f3029e2fa444a931ea9beb5daa31` and Issue #162's
2026-09-08 owner update found no independently readable face segmentation resource.
Core exposes Original/Proxy/Neural/Motion/Exposure to composition; AutoMask,
SkinStructure and LocalTone are opaque model controls. ControlMask is an input,
not a demonstrated segmentation output. The real native NGX parameter probe
already passed both integer overloads: their difference is not an established
cause of the reported visual toggle bug.

CombineV2 attenuates log luminance edits with existing protections, then applies
the global transfer multiplier to both luminance and chroma. Nonzero model edits
remain subject to it; an exactly zero protected residual stays zero. This does
not establish the toggle bug's cause and is not a face-protection fix.

This test-only generator extracts the actual production shader/constants and
reuses the codec GPU harness. It replaces TransferStrength **inside CombineV2**
with `lerp(globalStrength, independentStrength, probability)`, including its
early identity branch and both luma/chroma exponents. Existing HDR, near-black,
edge, gamut, finite checks and alpha preservation remain after that choice.
Shipping source, descriptors, defaults and UI are not modified.

SyntheticMask is a current-frame 2x2 float probability texture over the 4x4
source-view rectangle. Pixel-center mapping is `(p+.5)*maskSize/viewSize-.5`;
four nonnegative bilinear weights sum to one, with clamped source indices.
There is no negative-lobe filter. The test borrows the unused MotionSource t3
descriptor for a float4 texture only in this isolated entry-point fixture; a
real adapter must allocate a separate SRV and must not overwrite live motion.
One appended root constant holds the independent strength. This is a proposed
seam, not a new public ABI.

Unknown validity, wrong frame/view/epoch or foreign owner is rejected before
dispatch, not converted into probability zero. Known probability zero is valid
background. The mock admission test has no fallback to another producer or stale
history. A production source must additionally supply extent/ROI/phase and
finite [0,1] payload validation, ownership and exact queue/fence retirement.
No old-mask reuse/reprojection is implemented or authorized. Missing masks must
report unavailable and keep the existing route, without claiming protection.

The small fixed group tests full-mask zero strict source identity, nonzero local
strength even when global is zero, full-mask independence from global strength,
and all four center/boundary columns against uniform-strength production shader
references. Nonuniform RGB channel edits exercise luma and chroma together.
Five CPU receipt rejects cover unknown/stale/view/epoch/owner. WARP is mandatory;
there is no hardware fallback. No model, NGX, recognition, game or quality claim.

Remaining options are an actual engine mask with a proven contract, an independently
evaluated segmentation model (additional inference/distribution/history cost), or
this synthetic-only mathematical seam. Skin-color heuristics are not a substitute
for face/person segmentation and are not introduced here. Actual source remains
unresolved; production integration and the reported bug remain open.

## Frozen verification

Source `355e53dc47720360959c6ab6d23ddc70950125c0` was generated and compiled with
LLVM-MinGW 20260826 (`-std=c++20 -O2 -static -Wall -Wextra -Werror`, linked with
d3d12/dxgi/dxguid/d3dcompiler). The executable compiled the extracted/injected
production HLSL and ran the one authorized fixed WARP group: **10 dispatches and
5 CPU admission rejects passed**, exit 0, process exited/GPU released. Full-mask
identity and all uniform/boundary comparisons are bitwise comparisons of actual
RGBA output (including alpha), not tolerance-only or a duplicate CPU compositor.
The source harness's sentinel-outside-ROI and finite/alpha checks also ran.
No prior failed GPU attempt or repeated matrix occurred.

| Private artifact | SHA256 |
| --- | --- |
| generated synthetic_mask.cpp | `8d889ccc566314adc247478240fd6ea353a41d766b91b217fe41e92044952dc7` |
| executable | `bdcebcb381d964d5ada3ce0af13d205f371b9dce55043c2a64615f5eb2df46f7` |
| run.log | `81e76fa7f2cc81b6437e43d1550dbc1eb0f5ab49ab13c0ea2aefb79686d6e7ab` |
| extraction/input manifest | `d8a53771cfdb0221f17a0058ad4893c20ec8e82f817359f51da1eaa88c258928` |

Reproduction prepares source only: `python tests/synthetic_mask/prepare.py <private-output-dir>`.
The generated manifest locks the production source, harness, generator and test
main. Preparation does not execute the GPU binary. This is test-local code and
does not establish a ready production resource/lifecycle implementation.

The later generator-only archival guard rejects an existing output directory,
validates all inputs/anchors before writing, and exclusively creates new files.
CPU checks confirmed refusal of the original evidence directory and new generation
with the same `8d889ccc...` source hash. The first guard check exposed newline
translation differences; these were corrected to retain the prior writer's bytes.
Original `355e53d` evidence was untouched and no GPU verification was repeated.
