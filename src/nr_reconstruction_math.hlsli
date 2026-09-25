// Independent output-only reconstruction math. Thresholds are experimental in
// the OWN decoded normalized preparation domain, not nits, PQ or RC10 constants.
#ifndef NR_RECONSTRUCTION_MATH_HLSLI
#define NR_RECONSTRUCTION_MATH_HLSLI
// NR_RECONSTRUCTION_MATH_BEGIN
float NrReconSmooth(float x) {
    float t = saturate(x);
    return t * t * (3.0f - 2.0f * t);
}
float NrReconChromaConfidence(float candidatePeak, float recoveryGain) {
    // Both risks must be present. Dark candidates at modest recovery gains and
    // adequately represented new colours remain completely untouched.
    if (!isfinite(candidatePeak) || !isfinite(recoveryGain) ||
        candidatePeak < 0.0f || recoveryGain <= 0.0f) return 1.0f;
    const float black = 1.0f / 4096.0f;
    const float reliable = 1.0f / 128.0f;
    float signal = NrReconSmooth((candidatePeak - black) / (reliable - black));
    float amplification = NrReconSmooth((log2(max(recoveryGain, 1.0f)) - 1.0f) * 0.5f);
    return max(signal, 1.0f - amplification);
}
// Nonnegative cubic B-spline support. Only the paired edits are reconstructed;
// original full-grid texture/alpha are never filtered or replaced by this kernel.
float NrReconCubicWeight(float distance) {
    float x = abs(distance);
    if (x < 1.0f) return (4.0f + x*x*(3.0f*x - 6.0f)) / 6.0f;
    if (x < 2.0f) { float t = 2.0f - x; return t*t*t / 6.0f; }
    return 0.0f;
}
float3 NrReconPreserveColour(float3 source, float3 prepared, float3 proxy,
    float3 neural, float3 result, bool boundedSdr, bool enabled) {
    if (!enabled || all(neural == proxy) || all(result == source)) return result;
    if (!all(isfinite(source)) || !all(isfinite(prepared)) ||
        !all(isfinite(proxy)) || !all(isfinite(neural)) || !all(isfinite(result)) ||
        any(source < 0.0f) || any(prepared < 0.0f) || any(result < 0.0f)) return result;
    float sourcePeak = ColourLabPeak(source);
    float preparedPeak = ColourLabPeak(prepared);
    float sourceLuma = dot(source, kLuma);
    float targetLuma = dot(result, kLuma);
    if (sourcePeak <= 0.000001f || sourceLuma <= 0.000001f ||
        preparedPeak <= 0.0f || preparedPeak > 1.0001f || targetLuma <= 0.0f) return result;
    // Evaluate the additive candidate in the exact SOURCE preparation scale,
    // matching ColourLabRestoreRgb without magnifying HDR values first.
    float scale = preparedPeak / sourcePeak;
    float3 candidate = source * scale + (neural - proxy);
    float candidateLuma = dot(candidate, kLuma);
    float targetPrepared = targetLuma * scale;
    if (!all(isfinite(candidate)) || any(candidate < 0.0f) ||
        !isfinite(candidateLuma) || !isfinite(targetPrepared) || targetPrepared <= 0.0f) return result;
    float gain = targetPrepared / max(candidateLuma, 0.000000000001f);
    float trust = NrReconChromaConfidence(ColourLabPeak(candidate), gain);
    if (trust >= 1.0f) return result; // Credible new colours: exact old result.
    float3 sourceAtNewLight = source * (targetLuma / sourceLuma);
    // Keep the SAME already-selected new luminance. Gamut handling is necessary
    // when scaling saturated SDR to a brighter target; never clamp RGB per channel.
    sourceAtNewLight = ColourLabGamut(sourceAtNewLight, targetLuma, boundedSdr);
    float3 stable = trust <= 0.0f ? sourceAtNewLight : lerp(sourceAtNewLight, result, trust);
    return all(isfinite(stable)) ? stable : result;
}
// NR_RECONSTRUCTION_MATH_END
bool NrReconChromaRequested() { return (RegionFlags & 1024u) != 0u; }
#endif
