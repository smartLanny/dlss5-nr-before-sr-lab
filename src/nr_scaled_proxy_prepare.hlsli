// D21 colour-lab: optional encode-first scaling for sub-native NR.
// Expanded immediately after FilterExactAreaV6. It changes no resources or
// passes: the same exact-area footprint is averaged after per-tap model-proxy
// preparation instead of averaging scene values before a nonlinear encode.
#ifndef NR_SCALED_PROXY_PREPARE_HLSLI
#define NR_SCALED_PROXY_PREPARE_HLSLI
bool ColourLabProxyFirstScaleRequested() {
    return (Passthrough & 67108864u) != 0u;
}
float3 ColourLabStoredProxyTap(int2 sourceCoord) {
    const bool modelValueDomain = (Passthrough & 1u) != 0u;
    const bool srgbSdrStorage = (Passthrough & 32u) != 0u;
    float3 stored = LoadSourceStoredV2(sourceCoord).rgb;
    if (modelValueDomain || srgbSdrStorage)
        return modelValueDomain ? saturate(stored) : stored;
    float3 sourceLinear = LoadSourceLinearV2(sourceCoord);
    float3 proxyLinear = EncodeProxyLinearV2(sourceLinear);
    return LinearToSRGB(proxyLinear);
}
float3 ColourLabFilterPreparedProxyExactArea(uint2 pixel) {
    float2 sourceStart = float2(pixel) *
        float2(SourceWidth, SourceHeight) / float2(Width, Height);
    float2 sourceEnd = float2(pixel + 1u) *
        float2(SourceWidth, SourceHeight) / float2(Width, Height);
    int2 first = int2(floor(sourceStart));
    int2 last = int2(ceil(sourceEnd));
    float3 total = 0.0.xxx;
    float totalWeight = 0.0f;
    [loop] for (int y = first.y; y < last.y; ++y) {
        float wy = max(0.0f, min(sourceEnd.y, float(y + 1)) -
                              max(sourceStart.y, float(y)));
        [loop] for (int x = first.x; x < last.x; ++x) {
            float wx = max(0.0f, min(sourceEnd.x, float(x + 1)) -
                                  max(sourceStart.x, float(x)));
            float w = wx * wy;
            float3 sampleValue = ColourLabStoredProxyTap(int2(x, y));
            if (!all(isfinite(sampleValue))) sampleValue = 0.0.xxx;
            total += sampleValue * w;
            totalWeight += w;
        }
    }
    return total / max(totalWeight, 0.000001f);
}
#endif
