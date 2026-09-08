#ifndef NR_SKIN_PROTECTION_H
#define NR_SKIN_PROTECTION_H

// #208 composition-only experiment. This file is the common C++/SM5 scalar
// implementation, not a face detector and not a runtime mask resource owner.
// Define NR_SKIN_HLSL before concatenating it into the production shader.
#ifndef NR_SKIN_HLSL
#include <cmath>
#include <cstdint>
namespace nr_skin_protection {
#define NR_SKIN_INLINE inline
NR_SKIN_INLINE bool NrSkinFinite(float v) { return std::isfinite(v); }
NR_SKIN_INLINE float NrSkinSqrt(float v) { return std::sqrt(v); }
#else
#define NR_SKIN_INLINE
// Bit classification cannot be optimised into true under SM5 fast-math.
bool NrSkinFinite(float v) { return (asuint(v) & 0x7f800000u) != 0x7f800000u; }
float NrSkinSqrt(float v) { return sqrt(v); }
#endif

struct NrSkinRgb { float r; float g; float b; };
struct NrSkinMask { float confidence; bool valid; };
struct NrSkinRequest { bool enabled; float preserveLuma; float preserveColour; };
static const int NrSkinDisabled = 0;
static const int NrSkinInvalidMask = 1;
static const int NrSkinZeroSelection = 2;
static const int NrSkinInvalidRequest = 3;
static const int NrSkinZeroProtection = 4;
static const int NrSkinInvalidComposition = 5;
static const int NrSkinUndefinedChroma = 6;
static const int NrSkinOutputOutsideDomain = 7;
static const int NrSkinApplied = 8;
struct NrSkinResult { NrSkinRgb rgb; bool applied; int reason; };

NR_SKIN_INLINE NrSkinRgb NrSkinRgbValue(float r, float g, float b) {
    NrSkinRgb v; v.r = r; v.g = g; v.b = b; return v;
}
NR_SKIN_INLINE NrSkinMask NrSkinMaskValue(float confidence, bool valid) {
    NrSkinMask m; m.confidence = confidence; m.valid = valid; return m;
}
NR_SKIN_INLINE NrSkinRequest NrSkinDefaultRequest() {
    NrSkinRequest r;
    r.enabled = false; r.preserveLuma = 0.0f; r.preserveColour = 0.0f;
    return r;
}
NR_SKIN_INLINE float NrSkinClamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
NR_SKIN_INLINE float NrSkinSmooth(float low, float high, float v) {
    float t = NrSkinClamp01((v - low) / (high - low));
    return t * t * (3.0f - 2.0f * t);
}
NR_SKIN_INLINE bool NrSkinRgbInRange(NrSkinRgb v, float maximum) {
    return NrSkinFinite(v.r) && NrSkinFinite(v.g) && NrSkinFinite(v.b) &&
        v.r >= 0.0f && v.g >= 0.0f && v.b >= 0.0f &&
        v.r <= maximum && v.g <= maximum && v.b <= maximum;
}
NR_SKIN_INLINE float NrSkinLuma709(NrSkinRgb v) {
    return 0.2126f * v.r + 0.7152f * v.g + 0.0722f * v.b;
}

// Input: unmodified, exposure-matched, sRGB-encoded BT.709 display reference
// in [0,1]. PQ, scRGB, raw linear or unknown-domain numbers are NOT accepted.
// The host must establish the contract; a texture format is not that proof.
// Cb/Cr are BT.709 Y' colour-difference coordinates, without code-value offset.
// The conservative ellipse/ramp is a calibration seed, not a validated skin
// gamut. Warm materials can match, while coloured light/anime skin can miss.
NR_SKIN_INLINE NrSkinMask NrSkinClassify(NrSkinRgb originalDisplayReference,
                                        bool referenceAdmitted) {
    if (!referenceAdmitted || !NrSkinRgbInRange(originalDisplayReference, 1.0f))
        return NrSkinMaskValue(0.0f, false);
    float y = NrSkinLuma709(originalDisplayReference);
    float cb = (originalDisplayReference.b - y) / 1.8556f;
    float cr = (originalDisplayReference.r - y) / 1.5748f;
    float x = (cb + 0.075f) / 0.090f;
    float z = (cr - 0.105f) / 0.115f;
    float radiusSquared = x * x + z * z;
    float chroma = NrSkinSqrt(cb * cb + cr * cr);
    float ellipse = 1.0f - NrSkinSmooth(0.30f, 1.0f, radiusSquared);
    float chromaGate = NrSkinSmooth(0.025f, 0.060f, chroma);
    float darkGate = NrSkinSmooth(0.030f, 0.100f, y);
    // Valid all-zero confidence means a supported reference with no selected
    // colour, not a missing mask or evidence of a model AutoMask failure.
    return NrSkinMaskValue(NrSkinClamp01(ellipse * chromaGate * darkGate), true);
}

// The two RGB inputs are the original and current candidate at ONE composition
// point in the SAME finite, nonnegative linear-BT.709 working domain/exposure.
// This is a separable luminance/normalised-chroma edit attenuation:
//   a=M*preserveLuma, c=M*preserveColour
//   Yout=(1-a)*Ycandidate+a*Yoriginal
//   Qout=(1-c)*(candidate/Ycandidate)+c*(original/Yoriginal)
//   out=Yout*Qout
// Pure colour protection preserves candidate luminance; pure luma protection
// preserves candidate chromaticity. Both full protection returns exact original.
// Degenerate chroma, negative/invalid RGB, bad requests or an invalid mask
// return the current candidate unchanged. Existing final gamut/headroom/ratio
// budget still belongs to the caller; this helper does not add a second budget.
// Alpha is outside this RGB API and MUST retain the original production alpha.
NR_SKIN_INLINE NrSkinResult NrSkinProtect(NrSkinRgb original, NrSkinRgb candidate,
                                         NrSkinMask mask, NrSkinRequest request) {
    NrSkinResult result; result.rgb = candidate; result.applied = false; result.reason = NrSkinDisabled;
    if (!request.enabled) return result;
    result.reason = NrSkinInvalidMask;
    if (!mask.valid || !NrSkinFinite(mask.confidence) ||
        mask.confidence < 0.0f || mask.confidence > 1.0f) return result;
    result.reason = NrSkinZeroSelection;
    if (mask.confidence == 0.0f) return result;
    result.reason = NrSkinInvalidRequest;
    if (!NrSkinFinite(request.preserveLuma) || !NrSkinFinite(request.preserveColour) ||
        request.preserveLuma < 0.0f || request.preserveLuma > 1.0f ||
        request.preserveColour < 0.0f || request.preserveColour > 1.0f)
        return result;
    float lumaWeight = mask.confidence * request.preserveLuma;
    float colourWeight = mask.confidence * request.preserveColour;
    result.reason = NrSkinZeroProtection;
    if (lumaWeight == 0.0f && colourWeight == 0.0f) return result;
    result.reason = NrSkinInvalidComposition;
    if (!NrSkinRgbInRange(original, 65504.0f) || !NrSkinRgbInRange(candidate, 65504.0f))
        return result;
    if (lumaWeight == 1.0f && colourWeight == 1.0f) {
        result.rgb = original; result.applied = true; result.reason = NrSkinApplied; return result;
    }
    if (original.r == candidate.r && original.g == candidate.g && original.b == candidate.b) {
        result.applied = true; result.reason = NrSkinApplied; return result;
    }
    float originalY = NrSkinLuma709(original);
    float candidateY = NrSkinLuma709(candidate);
    // Do not invent chromaticity at black. Full protection above has a defined
    // exact endpoint even at black; partial independent requests fail closed.
    result.reason = NrSkinUndefinedChroma;
    if (originalY <= 1.0f / 65536.0f || candidateY <= 1.0f / 65536.0f) return result;
    float outputY = candidateY + lumaWeight * (originalY - candidateY);
    NrSkinRgb qCandidate = NrSkinRgbValue(candidate.r / candidateY,
        candidate.g / candidateY, candidate.b / candidateY);
    NrSkinRgb qOriginal = NrSkinRgbValue(original.r / originalY,
        original.g / originalY, original.b / originalY);
    NrSkinRgb q = NrSkinRgbValue(
        qCandidate.r + colourWeight * (qOriginal.r - qCandidate.r),
        qCandidate.g + colourWeight * (qOriginal.g - qCandidate.g),
        qCandidate.b + colourWeight * (qOriginal.b - qCandidate.b));
    NrSkinRgb output = NrSkinRgbValue(outputY * q.r, outputY * q.g, outputY * q.b);
    result.reason = NrSkinOutputOutsideDomain;
    if (!NrSkinRgbInRange(output, 65504.0f)) return result;
    result.rgb = output; result.applied = true; result.reason = NrSkinApplied; return result;
}

#ifndef NR_SKIN_HLSL
// CPU admission metadata; never upload its bool/padding layout as an HLSL CB.
// Runtime translates admitted state into explicit scalar shader constants.
enum class ReferenceEncoding : std::uint32_t { Unknown, Srgb709Display };
enum class ReferenceOrigin : std::uint32_t { Unknown, OriginalBeforeNr, NrOutput };
struct InputIdentity {
    bool valid = false;
    ReferenceEncoding encoding = ReferenceEncoding::Unknown;
    ReferenceOrigin origin = ReferenceOrigin::Unknown;
    bool exposureMatched = false;
    bool compositionLinear709 = false;
    std::uint64_t sourceFrame = 0, sourceGeneration = 0, view = 0, exposureGeneration = 0;
    std::uint64_t colourContractGeneration = 0;
    std::uint32_t width = 0, height = 0;
    std::uint32_t rectX = 0, rectY = 0, rectWidth = 0, rectHeight = 0;
    float phaseX = 0, phaseY = 0;
};
inline bool AdmitReference(const InputIdentity& v) {
    return v.valid && v.encoding == ReferenceEncoding::Srgb709Display &&
        v.origin == ReferenceOrigin::OriginalBeforeNr && v.exposureMatched &&
        v.compositionLinear709 && v.width > 0 && v.height > 0 &&
        v.rectWidth > 0 && v.rectHeight > 0 && v.rectX < v.width && v.rectY < v.height &&
        v.rectWidth <= v.width - v.rectX && v.rectHeight <= v.height - v.rectY &&
        std::isfinite(v.phaseX) && std::isfinite(v.phaseY);
}
inline bool SameSource(const InputIdentity& reference, const InputIdentity& composition) {
    return AdmitReference(reference) && AdmitReference(composition) &&
        reference.sourceFrame == composition.sourceFrame && reference.view == composition.view &&
        reference.sourceGeneration == composition.sourceGeneration &&
        reference.exposureGeneration == composition.exposureGeneration &&
        reference.colourContractGeneration == composition.colourContractGeneration &&
        reference.width == composition.width && reference.height == composition.height &&
        reference.rectX == composition.rectX && reference.rectY == composition.rectY &&
        reference.rectWidth == composition.rectWidth && reference.rectHeight == composition.rectHeight &&
        reference.phaseX == composition.phaseX && reference.phaseY == composition.phaseY;
}
struct MaskIdentity {
    InputIdentity source;
    std::uint64_t calibrationGeneration = 0;
};
inline bool AdmitMask(const MaskIdentity& mask, const InputIdentity& composition,
                      std::uint64_t calibrationGeneration) {
    return SameSource(mask.source, composition) &&
        mask.calibrationGeneration == calibrationGeneration;
}
// Validated nonnegative interpolation only. No negative-kernel interpolation of
// confidence or validity. The caller supplies same-source taps and may already
// have applied source-edge weights. An invalid contributing tap rejects the
// result instead of quietly expanding/renormalising the selected region.
inline NrSkinMask WeightedMask(const NrSkinMask* taps, const float* weights, unsigned count) {
    if (!taps || !weights || count == 0) return NrSkinMaskValue(0.0f, false);
    float total = 0.0f, selected = 0.0f;
    for (unsigned i = 0; i < count; ++i) {
        if (!std::isfinite(weights[i]) || weights[i] < 0.0f) return NrSkinMaskValue(0.0f, false);
        if (weights[i] == 0.0f) continue;
        if (!taps[i].valid || !std::isfinite(taps[i].confidence) ||
            taps[i].confidence < 0.0f || taps[i].confidence > 1.0f)
            return NrSkinMaskValue(0.0f, false);
        total += weights[i]; selected += weights[i] * taps[i].confidence;
    }
    if (!std::isfinite(total) || !std::isfinite(selected) || total <= 0.0f)
        return NrSkinMaskValue(0.0f, false);
    return NrSkinMaskValue(NrSkinClamp01(selected / total), true);
}
} // namespace nr_skin_protection
#endif
#undef NR_SKIN_INLINE
#endif
