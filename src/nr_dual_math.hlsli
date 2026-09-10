#ifndef NR_DUAL_MATH_HLSLI
#define NR_DUAL_MATH_HLSLI
// Domain 0: existing direct/model-value path. Domain 1: existing sRGB Proxy path.
// Never call these functions on raw PQ, scRGB scene light or a signed delta.
bool NRDualFinite(float3 v) {
    return all(isfinite(v)) && all(abs(v) <= 65504.0.xxx);
}
float NRDualDecodeChannel(float x) {
    return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}
float NRDualEncodeChannel(float x) {
    // Scalar branch avoids a fractional power of a negative residual-containing value.
    return x <= 0.0031308 ? x * 12.92 : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}
float3 NRDualDecode(float3 v, uint domain) {
    if (domain == 0u) return v;
    return float3(NRDualDecodeChannel(v.r), NRDualDecodeChannel(v.g), NRDualDecodeChannel(v.b));
}
float3 NRDualEncode(float3 v, uint domain) {
    if (domain == 0u) return v;
    return float3(NRDualEncodeChannel(v.r), NRDualEncodeChannel(v.g), NRDualEncodeChannel(v.b));
}
#endif
