#ifndef NR_DUAL_RESOLVE_HLSLI
#define NR_DUAL_RESOLVE_HLSLI
#include "nr_dual_math.hlsli"
// Define these four symbols to actual, correctly typed SRVs before including:
// NR_DUAL_MAIN: Texture2D<float4>, first pass answer, at main model extent
// NR_DUAL_LOW_IN / NR_DUAL_LOW_OUT: Texture2D<float4>, at secondary extent
// NR_DUAL_VALID: Texture2D<uint>, R32_UINT written by DownsampleCS
// This code is called at a MAIN-MODEL pixel, before the existing N->R reconstruction.
// 100% bypasses it on the CPU and binds the second answer directly.
float4 NRDualFinalStored(int2 mainPixel, uint2 mainSize, uint2 lowSize, uint domain) {
    mainPixel = clamp(mainPixel, int2(0, 0), int2(mainSize) - 1);
    float4 primary = NR_DUAL_MAIN.Load(int3(mainPixel, 0));
    if (!NRDualFinite(primary.rgb) || domain > 1u || any(lowSize == 0u)) return primary;
    float2 p = (float2(mainPixel) + 0.5) * float2(lowSize) / float2(mainSize) - 0.5;
    int2 base = int2(floor(p));
    float2 fraction = p - floor(p);
    float3 delta = 0.0.xxx;
    bool valid = true;
    [loop] for (int dy = 0; dy != 2; ++dy) {
        [loop] for (int dx = 0; dx != 2; ++dx) {
            float w = (dx == 0 ? 1.0 - fraction.x : fraction.x) *
                      (dy == 0 ? 1.0 - fraction.y : fraction.y);
            if (w <= 0.0) continue;
            int2 q = clamp(base + int2(dx, dy), int2(0, 0), int2(lowSize) - 1);
            if (NR_DUAL_VALID.Load(int3(q, 0)) == 0u) { valid = false; continue; }
            float3 input = NR_DUAL_LOW_IN.Load(int3(q, 0)).rgb;
            float3 output = NR_DUAL_LOW_OUT.Load(int3(q, 0)).rgb;
            if (!NRDualFinite(input) || !NRDualFinite(output)) { valid = false; continue; }
            // Matched at low resolution BEFORE interpolation. Do not decode delta,
            // subtract P0, or compare the low answer against the full-size picture.
            float3 d = NRDualDecode(output, domain) - NRDualDecode(input, domain);
            if (!all(isfinite(d))) { valid = false; continue; }
            delta += w * d;
        }
    }
    // Exact no-op path avoids decode/re-encode drift for an identity second layer.
    if (!valid || all(delta == 0.0.xxx)) return primary;
    float3 candidate = NRDualEncode(NRDualDecode(primary.rgb, domain) + delta, domain);
    if (!NRDualFinite(candidate)) return primary;
    // No saturate(delta), sharpening, re-exposure, new temporal history or tone map.
    // Keep main alpha. The FINAL existing codec must still preserve original game alpha.
    return float4(candidate, primary.a);
}
#endif
