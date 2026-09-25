// Optional paired HDR preparation. Expanded before the existing HDR encoders.
// The default keeps each D21 encoder unchanged. Bounded SDR and unresolved
// model-value domains never acquire an inferred HDR transfer function.
#ifndef NR_COLOUR_PREPARE_HLSLI
#define NR_COLOUR_PREPARE_HLSLI
bool ColourLabN2Requested() {
    return (Passthrough & 268435456u) != 0u &&
        (Passthrough & (1u | 16u | 32u)) == 0u;
}
// NR_COLOUR_PREPARE_MATH_BEGIN
float3 ColourLabN2Prepare(float3 scene) {
    if (!all(isfinite(scene))) return float3(0.0f, 0.0f, 0.0f);
    scene = max(scene, 0.0f);
    float peak = max(scene.x, max(scene.y, scene.z));
    // The current source preparation is bounded well below sqrt(FLT_MAX).
    // Keep hostile/extreme input from forming infinity in peak*peak anyway.
    if (peak > 1.0e18f) return float3(0.0f, 0.0f, 0.0f);
    return scene * rsqrt(1.0f + peak * peak);
}
// NR_COLOUR_PREPARE_MATH_END
#endif
