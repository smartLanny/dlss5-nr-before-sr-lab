// D21 colour-lab: matched RGB edits with source-referenced preparation scale.
// Expanded into the real codec string by patch-colour-restore.py. No new texture,
// history, model invocation, root parameter, or GPU lifetime is introduced.
// This is an independent implementation, not a copy of the RenoDX addon.
#ifndef NR_COLOUR_RESTORE_HLSLI
#define NR_COLOUR_RESTORE_HLSLI

// These two bits are reserved by the colour-lab overlay, not by uniform3.
// bit 30 selects this final consumer; bit 29 identifies the actual EncodeCS
// preparation even when a saved experiment selects CombineV2CS after SR/RR.
bool ColourLabRgbEnabled() { return (Passthrough & 1073741824u) != 0u; }
bool ColourLabPostEncoding() { return (Passthrough & 536870912u) != 0u; }
bool ColourLabRawEditRequested() { return (Passthrough & 134217728u) != 0u; }

// NR_COLOUR_RESTORE_MATH_BEGIN
// Pure production math. The host test compiles this exact section with a
// minimal float3 adapter; it does not substitute a second algorithm.
struct ColourLabRgbResult {
    float3 rgb;
    bool valid;
};

float ColourLabPeak(float3 value) { return max(value.x, max(value.y, value.z)); }
float ColourLabAbsPeak(float3 value) {
    return max(abs(value.x), max(abs(value.y), abs(value.z)));
}

// Existing lighting/detail/region/face controls produce a bounded relative
// luminance edit. Convert their final edit to a gain on the matched RGB edit.
// Equal raw/protected edits stay exactly 1x. A zero/sign-reversed edit cannot
// generate a new luminance contribution. The caller
// retains the existing model-delta zero gate; finite output is checked below.
float ColourLabLumaGain(float rawRelative, float protectedRelative) {
    if (!isfinite(rawRelative) || !isfinite(protectedRelative)) return 0.0f;
    if (rawRelative == 0.0f || protectedRelative == 0.0f) return 0.0f;
    if ((rawRelative < 0.0f) != (protectedRelative < 0.0f)) return 0.0f;
    return protectedRelative / rawRelative;
}

// One scalar for an entire RGB vector: zero source channels must not force
// every colour edit to zero. The cap is relative to the full source's peak,
// not a per-channel source ratio. A user's MaxLumaRatio=1 still means no edit.
float ColourLabChromaBudget(float3 edit, float sourcePeak, float targetLuma,
                            float maxRatio) {
    float peak = ColourLabAbsPeak(edit);
    if (peak == 0.0f) return 1.0f;
    float budget = max(sourcePeak, targetLuma) * max(maxRatio - 1.0f, 0.0f);
    return min(1.0f, budget / peak);
}

// Preserve luminance while shrinking the chroma vector only when it leaves
// the permitted gamut. In-gamut RGB is returned without a round trip. The
// SDR ceiling is never applied to a scene-linear/PQ HDR preparation.
float3 ColourLabGamut(float3 rgb, float luma, bool boundedSdr) {
    float3 neutral = float3(luma, luma, luma);
    float3 delta = rgb - neutral;
    float scale = 1.0f;
    [unroll] for (uint i = 0u; i < 3u; ++i) {
        if (delta[i] < 0.0f) scale = min(scale, luma / -delta[i]);
        else if (boundedSdr && delta[i] > 0.0f)
            scale = min(scale, max(1.0f - luma, 0.0f) / delta[i]);
    }
    return scale >= 1.0f ? rgb : neutral + delta * max(scale, 0.0f);
}

ColourLabRgbResult ColourLabRestoreRgb(
    float3 source, float3 prepared, float3 proxy, float3 neural,
    float rawRelative, float protectedRelative, float colorStrength,
    float confidence, float maxRatio, float regionColor,
    float faceMask, float faceOriginalColor, bool faceExtraCap,
    bool boundedSdr, bool rawEditReference) {
    ColourLabRgbResult outValue;
    outValue.rgb = source;
    outValue.valid = false;
    if (!all(isfinite(source)) || !all(isfinite(prepared)) ||
        !all(isfinite(proxy)) || !all(isfinite(neural)) ||
        any(source < 0.0f) || any(prepared < 0.0f) ||
        !isfinite(rawRelative) || !isfinite(protectedRelative) ||
        !isfinite(colorStrength) || !isfinite(confidence) ||
        !isfinite(maxRatio) || !isfinite(regionColor) ||
        !isfinite(faceMask) || !isfinite(faceOriginalColor)) return outValue;
    outValue.valid = true;
    float3 delta = neural - proxy; // Already decoded on the same paired grid.
    float effectiveConfidence = rawEditReference ? 1.0f : saturate(confidence);
    if (all(delta == 0.0f) || effectiveConfidence <= 0.0f) return outValue;
    float sourceLuma = dot(source, kLuma);
    float sourcePeak = ColourLabPeak(source);
    float preparedPeak = ColourLabPeak(prepared);
    if (sourceLuma <= 0.000001f || sourcePeak <= 0.000001f) return outValue;
    if (preparedPeak <= 0.0f) { outValue.valid = false; return outValue; }

    // Invert the SOURCE'S forward scale, never an inverse curve evaluated on
    // the edited neural result. This also leaves full-source high frequencies
    // intact when the proxy/model pair is reconstructed from a smaller grid.
    float inverseSourceScale = sourcePeak / preparedPeak;
    float3 sourceDelta = delta * inverseSourceScale;
    float deltaLuma = dot(sourceDelta, kLuma);
    // Diagnostic raw reference: answer whether the model already created the
    // missing structure. Aesthetic luminance attenuation is bypassed, while
    // finite/gamut/max-ratio and all GPU/resource safety stay intact.
    float lumaGain = rawEditReference ? 1.0f
                                      : ColourLabLumaGain(rawRelative, protectedRelative);
    maxRatio = max(maxRatio, 1.0f);
    float targetLuma = clamp(sourceLuma + deltaLuma * lumaGain,
                             sourceLuma / maxRatio, sourceLuma * maxRatio);
    if (boundedSdr) targetLuma = min(targetLuma, 1.0f);
    float3 lumaOnly = targetLuma == sourceLuma ? source : source * (targetLuma / sourceLuma);
    float3 result = lumaOnly;
    float candidateLuma = sourceLuma + deltaLuma;
    float colour = clamp(colorStrength, 0.0f, 2.0f) * effectiveConfidence;
    if (rawEditReference) {
        // Region and face attenuation are presentation choices, not validity
        // checks. Bypass them only in this explicit diagnostic reference.
        regionColor = 1.0f;
        faceMask = 0.0f;
        faceExtraCap = false;
    }
    if (colour > 0.0f && candidateLuma > 0.000001f) {
        // A luminance-neutral vector. At neutral protection, T=C=1 and a
        // representable in-gamut candidate this reconstructs source+delta/k.
        // Nonzero T changes luminance, not the independent colour coefficient.
        float3 candidate = source + sourceDelta;
        float3 chroma = (candidate * (targetLuma / candidateLuma) - lumaOnly) * colour;
        float3 sameNrChroma = chroma;
        chroma *= max(regionColor, 0.0f);
        faceMask = saturate(faceMask);
        if (faceExtraCap && faceMask > 0.0f) {
            float3 bounded = clamp(chroma, min(sameNrChroma, 0.0f), max(sameNrChroma, 0.0f));
            chroma = lerp(chroma, bounded, faceMask);
        }
        chroma *= 1.0f - faceMask * saturate(faceOriginalColor);
        chroma *= ColourLabChromaBudget(chroma, sourcePeak, targetLuma, maxRatio);
        result += chroma;
        // Per-component face-extra caps can alter the vector's luminance;
        // restore the protected target just as the original consumers do.
        float resultLuma = dot(result, kLuma);
        if (resultLuma > 0.000001f) result *= targetLuma / resultLuma;
        else result = lumaOnly;
    }
    result = ColourLabGamut(result, targetLuma, boundedSdr);
    if (!all(isfinite(result))) { outValue.valid = false; return outValue; }
    outValue.rgb = result;
    return outValue;
}
// NR_COLOUR_RESTORE_MATH_END

float3 ColourLabPreparedReference(float3 storedOriginal, float3 source) {
    if ((Passthrough & (1u | 16u | 32u)) != 0u) return saturate(source);
    if (!ColourLabPostEncoding()) return EncodeProxyLinearV2(source);
    // Post may use CombineV2CS but its input was still prepared by EncodeCS.
    return (Passthrough & 128u) != 0u
        ? PqSourceToModelLinear(storedOriginal)
        : HdrSourceToModelLinear(storedOriginal);
}
#endif
