// Optional colour4 edge recovery. Positive, source-guided weights are
// IDENTICAL for P and N. No new texture/pass; actual final-layer accessors stay.
#ifndef NR_COLOUR_EDGE_HLSLI
#define NR_COLOUR_EDGE_HLSLI
bool ColourEdgeRequested() { return (Passthrough & 16777216u) != 0u; }

// NR_COLOUR_PAIR_MATH_BEGIN
struct ColourLabPair {
    float3 proxy;
    float3 neural;
    float weight;
    bool valid;
};
ColourLabPair ColourLabEmptyPair() {
    ColourLabPair p;
    p.proxy = float3(0.0f, 0.0f, 0.0f);
    p.neural = p.proxy;
    p.weight = 0.0f;
    p.valid = true;
    return p;
}
float ColourLabGuideWeight(float3 reference, float3 proxy) {
    // Normalize first to avoid overflow for open-ended but finite HDR values.
    float scale = max(max(ColourLabAbsPeak(reference), ColourLabAbsPeak(proxy)), 1.0f/4096.0f);
    float distance = ColourLabAbsPeak(reference / scale - proxy / scale);
    return 1.0f / (1.0f + 64.0f * distance * distance);
}
ColourLabPair ColourLabAddPair(ColourLabPair p, float3 reference,
                               float3 proxy, float3 neural, float spatialWeight) {
    if (!all(isfinite(reference)) || !all(isfinite(proxy)) || !all(isfinite(neural)) ||
        !isfinite(spatialWeight) || spatialWeight < 0.0f) {
        p.valid = false;
        return p;
    }
    float weight = spatialWeight * ColourLabGuideWeight(reference, proxy);
    p.proxy += proxy * weight;
    p.neural += neural * weight;
    p.weight += weight;
    return p;
}
ColourLabPair ColourLabFinishPair(ColourLabPair p) {
    if (!p.valid || !isfinite(p.weight) || p.weight <= 0.0f) {
        p.valid = false;
        return p;
    }
    p.proxy = p.proxy / p.weight;
    p.neural = p.neural / p.weight;
    p.valid = all(isfinite(p.proxy)) && all(isfinite(p.neural));
    return p;
}
// NR_COLOUR_PAIR_MATH_END

ColourLabPair ColourEdgeSamplePair(float2 center, float3 reference, bool neutral) {
    ColourLabPair p = ColourLabEmptyPair();
    // Keep original sampling expressions for the disabled and 100% cases.
    if (!ColourEdgeRequested() || (Width == ModelWidth && Height == ModelHeight)) {
        p.proxy = SampleProxyLinearV2(center);
        p.neural = neutral ? ColourStageSampleNeural(center) : SampleNeuralLinearV2(center);
        p.weight = 1.0f;
        p.valid = all(isfinite(p.proxy)) && all(isfinite(p.neural));
        return p;
    }
    float2 mapped = center * float2(ModelWidth, ModelHeight) /
        float2(max(Width,1u), max(Height,1u)) - 0.5;
    int2 base = int2(floor(mapped));
    float2 f = frac(mapped);
    [unroll] for (uint i=0u; i<4u; ++i) {
        int2 offset = int2(i & 1u, i >> 1u);
        float w = (offset.x != 0 ? f.x : 1.0-f.x) * (offset.y != 0 ? f.y : 1.0-f.y);
        float3 neural = neutral ? ColourStageLoadNeural(base+offset) : LoadNeuralLinearV2(base+offset);
        p = ColourLabAddPair(p, reference, LoadProxyLinearV2(base+offset), neural, w);
    }
    return ColourLabFinishPair(p);
}
#endif
