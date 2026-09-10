#include "nr_dual_math.hlsli"
// The source is an owned compact first-pass output, not a game's padded Color atlas.
Texture2D<float4> MainAnswer : register(t0);
RWTexture2D<float4> LowInput : register(u0);     // R16G16B16A16_FLOAT
RWTexture2D<uint> LowValid : register(u1);       // R32_UINT, validity is not image alpha
cbuffer DualDownConstants : register(b0) {
    uint2 MainSize;
    uint2 LowSize;
    uint ModelDomain;
    uint3 Reserved;
};
[numthreads(8, 8, 1)]
void DownsampleCS(uint3 tid : SV_DispatchThreadID) {
    if (any(tid.xy >= LowSize)) return;
    float2 a = float2(tid.xy) * float2(MainSize) / float2(LowSize);
    float2 b = float2(tid.xy + 1u) * float2(MainSize) / float2(LowSize);
    int2 first = int2(floor(a));
    int2 end = min(int2(ceil(b)), int2(MainSize));
    float3 sum = 0.0.xxx;
    float total = 0.0;
    bool valid = ModelDomain <= 1u;
    // At 25-100% each axis intersects at most five input texels; exact overlap
    // weights are used at non-integer ratios. No point-sample minification.
    [loop] for (int y = first.y; y < end.y; ++y) {
        float wy = max(0.0, min(b.y, float(y + 1)) - max(a.y, float(y)));
        [loop] for (int x = first.x; x < end.x; ++x) {
            float wx = max(0.0, min(b.x, float(x + 1)) - max(a.x, float(x)));
            float w = wx * wy;
            if (w <= 0.0) continue;
            float3 stored = MainAnswer.Load(int3(x, y, 0)).rgb;
            if (!NRDualFinite(stored)) { valid = false; continue; }
            float3 linearValue = NRDualDecode(stored, ModelDomain);
            if (!all(isfinite(linearValue))) { valid = false; continue; }
            sum += linearValue * w;
            total += w;
        }
    }
    float3 encoded = 0.0.xxx;
    if (valid && total > 0.0) encoded = NRDualEncode(sum / total, ModelDomain);
    valid = valid && total > 0.0 && NRDualFinite(encoded);
    // Invalid input is not sent to NGX as NaN. LowValid prevents that local edit
    // being transferred. Global model influence is NOT bounded by this local mask;
    // the Core must log/inspect invalid model output rather than claim a full repair.
    LowInput[tid.xy] = float4(valid ? encoded : 0.0.xxx, 1.0);
    LowValid[tid.xy] = valid ? 1u : 0u;
}
