// Colour4: an early, bounded reference consumer, BEFORE aesthetic processing.
// The real NR output/history is never overwritten by the neutral instrument.
#ifndef NR_COLOUR_STAGE_REFERENCE_HLSLI
#define NR_COLOUR_STAGE_REFERENCE_HLSLI

bool ColourStageNeutral() { return (Passthrough & 33554432u) != 0u; }

// This loader is used by the copied *production neural sampling expression*
// below. P still uses the independent production proxy sampler. Thus the
// neutral test exercises their coordinate/clamp/decode/interpolation pairing,
// not a shortcut assigning neural=proxy after sampling. It does NOT validate
// writes to the real NeuralColor texture or NVIDIA inference.
float3 ColourStageLoadNeural(int2 p) {
    return ColourStageNeutral() ? LoadProxyLinearV2(p) : LoadNeuralLinearV2(p);
}
// NR_COLOUR_STAGE_NEURAL_SAMPLER

// NR_COLOUR_STAGE_MATH_BEGIN
ColourLabRgbResult ColourStageRestore(
    float3 source, float3 prepared, float3 proxy, float3 neural,
    bool inputValid, float transfer, float frameConfidence,
    float colorStrength, float maxRatio) {
    ColourLabRgbResult value;
    value.rgb = source;
    value.valid = false;
    if (!inputValid || !isfinite(transfer) || !isfinite(frameConfidence) ||
        !all(isfinite(source)) || !all(isfinite(prepared)) ||
        !all(isfinite(proxy)) || !all(isfinite(neural))) return value;
    // These are master/application-readiness gates, not aesthetic attenuation.
    // Preserve the original exactly while disabled or not yet ready.
    if (transfer <= 0.000001f || frameConfidence <= 0.000001f) {
        value.valid = true;
        return value;
    }
    return ColourLabRestoreRgb(source, prepared, proxy, neural,
        0.0f, 0.0f, colorStrength, 1.0f, maxRatio, 1.0f,
        0.0f, 0.0f, false, (Passthrough & (1u | 16u | 32u)) != 0u, true);
}
// NR_COLOUR_STAGE_MATH_END

struct ColourStagePixel {
    float4 stored;
    float3 proxy;
    float3 neural;
    bool invalid;
    float relativeDelta;
};
ColourStagePixel ColourStageReference(uint2 pixel, float4 original, bool mapped) {
    ColourStagePixel answer;
    answer.stored = original;
    answer.relativeDelta = 0.0f;
    float2 center = float2(pixel) + 0.5;
    float3 source = DecodeStoredForV2(original.rgb);
    float3 prepared = ColourLabPreparedReference(original.rgb, source);
    answer.proxy = mapped ? SampleProxyLinearV2(center) : LoadProxyLinearV2(int2(pixel));
    answer.neural = mapped ? ColourStageSampleNeural(center) : ColourStageLoadNeural(int2(pixel));
    // Even the synthetic probe must not turn malformed actual model data into
    // a successful sample. Keep the real texture's strict finite footprint.
    bool valid = mapped ? ModelNeighborhoodFiniteV2(center)
        : IsFiniteModelStoredV2(LoadFinalNeuralStoredV2(int2(pixel)).rgb);
    valid = valid && IsFiniteModelStoredV2(original.rgb) &&
        IsFiniteModelStoredV2(answer.proxy) && IsFiniteModelStoredV2(answer.neural) &&
        all(abs(source) <= 65504.0.xxx) && !any(original.rgb < 0.0.xxx);
    ColourLabRgbResult value = ColourStageRestore(source, prepared,
        answer.proxy, answer.neural, valid, TransferStrength, FrameConfidence,
        ColorStrength, MaxLumaRatio);
    answer.invalid = !value.valid;
    if (!value.valid || all(value.rgb == source)) return answer;
    float3 stored = EncodeCompositionToStoredV2(value.rgb);
    if (!IsFiniteModelStoredV2(stored)) { answer.invalid = true; return answer; }
    answer.stored = float4(stored, original.a);
    answer.relativeDelta = ColourLabAbsPeak(stored - original.rgb) /
        max(ColourLabAbsPeak(original.rgb), 0.000001f);
    return answer;
}
#endif
