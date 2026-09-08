# Magpie layer-control comparison

The owner asked to prioritize Magpie because its reflection enhancement is
visibly stronger. This source comparison explains a plausible difference in
control scope; no same-frame Magpie/D game benchmark was performed.

Official experimental HEAD checked on 2026-09-09:
[`ac1cc8b0f2efc78323898395cc1336bcbecdc276`](https://github.com/SAOG0721/Magpie/commit/ac1cc8b0f2efc78323898395cc1336bcbecdc276).
The relevant DLSSNRFilter.cpp blob is unchanged since `9824d758`, already
referenced in the earlier project notes. This is a closer reading of that
implementation, not a newly discovered upstream algorithm release.

The important distinction is coverage. Magpie classifies each pixel by the
sign of its overall luminance change, then scales the entire RGB residual for
the chosen reflection/glow or shadow/structure category. It does not split
the residual into broad and fine regions or independently suppress glow.
See its [classification and adjustment](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L360-L404).

The exposed total residual multiplier is 1–2, while direction, saturation and
lightness controls are 0–2. Total and direction multipliers can multiply to 4
before the later HSL lightness/saturation adjustment. Adjusted RGB residuals
are reconstructed and added back to the source with an SDR 0–1 output limit.
Sources: [parameter ranges](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Effects/DLSSNR/DLSSNR_AI_Filter.hlsl#L8-L77),
[residual reconstruction](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L408-L507).

For an unclipped grey SDR storage-value example, source .40 and NR .45 can
produce .80 with total 2, reflection 2 and lightness 2. This illustrates stacked
controls in that value domain; it is not a general eightfold linear-luminance
gain for arbitrary colours or HDR, nor evidence of stronger real reflections.

| Question | Magpie | Current D3/D4 |
| --- | --- | --- |
| What gets reflection gain? | Full RGB change for the positive-luminance category | Only the fine positive luminance component |
| What happens to broad brightening? | It shares reflection/glow gain | Separate broad control and glow suppression |
| Colour change | RGB residual plus HSL adjustments | Independent, policy-gated colour transfer |
| Reconstruction/output | Low-resolution residual, Catmull, SDR clamp | Existing per-route composition and HDR/lighting/edge safeguards |

With the owner's D3 Glow=1 and Broad=2, broad bright gain is zero. At a
half-broad/half-fine positive pixel, Reflection=1.91 gives total gain .955,
not 1.91. That differs sharply from Magpie's full positive category. It can
explain why controls appear less forceful, but the current TXT has no measured
partition pixels and cannot establish the exact cause in the owner's scene.

Magpie's controls require input-resolution adjustment enabled, even at 100%.
Its input check accepts SDR UNORM. Sources: [input contract](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L1880-L1900),
[enabled-path selection](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSNRFilter.cpp#L2359-L2370).
The D front path can use linear/HDR engine surfaces and native guides; its
automatic/PQ colour policy disables unverified chroma unless explicitly
requested. Copying SDR RGB addition, HSL adjustment or a 0–1 clamp into it
would alter exposure, colour admission and high-highlight behaviour together.

The next focused 0.5 comparison should hold original/proxy/neural planes,
model, work ratio and all protection settings fixed. Compare existing four
regions with a candidate that groups the complete positive/negative luma edit
at gains 1/2, initially keeping chroma off. Measure broad and fine regions
separately, then inspect protection attenuation separately. This tests whether
classification scope or protection chiefly limits visible response without
changing model and colour algorithms at the same time.

D4 is already frozen and installed for the authorized 0.4.7/NTE compatibility
test. This comparison does not change its shader, resolve its face issue or
claim an in-game reflection improvement. Only source documents were examined;
no upstream implementation or runtime was copied into the product.
