// Independently implemented continuous Generic controls. Oklab matrices are
// by Bjorn Ottosson (2021-01-25), public domain:
// https://bottosson.github.io/posts/oklab/
// No Magpie application code, SDR gamut search or protection thresholds are
// imported. The existing Core compositor owns final luminance/gamut limits.
#ifndef NR_GENERIC_CONTROLS_HLSLI
#define NR_GENERIC_CONTROLS_HLSLI
// NR_GENERIC_CONTROLS_MATH_BEGIN
struct GenericControlledV2 {
    float3 colorLinear;
    float lumaLinear;
    bool valid;
};
float GenericCubeRootV2(float v) {
    return v < 0.0f ? -pow(-v, 1.0f / 3.0f) : pow(v, 1.0f / 3.0f);
}
float3 GenericLinearToLabV2(float3 c) {
    float l = GenericCubeRootV2(dot(c, float3(0.4122214708f, 0.5363325363f, 0.0514459929f)));
    float m = GenericCubeRootV2(dot(c, float3(0.2119034982f, 0.6806995451f, 0.1073969566f)));
    float s = GenericCubeRootV2(dot(c, float3(0.0883024619f, 0.2817188376f, 0.6299787005f)));
    return float3(0.2104542553f*l + 0.7936177850f*m - 0.0040720468f*s,
        1.9779984951f*l - 2.4285922050f*m + 0.4505937099f*s,
        0.0259040371f*l + 0.7827717662f*m - 0.8086757660f*s);
}
float3 GenericLabToLinearV2(float3 c) {
    float l = c.x + 0.3963377774f*c.y + 0.2158037573f*c.z;
    float m = c.x - 0.1055613458f*c.y - 0.0638541728f*c.z;
    float s = c.x - 0.0894841775f*c.y - 1.2914855480f*c.z;
    l = l*l*l; m = m*m*m; s = s*s*s;
    return float3(4.0767416621f*l - 3.3077115913f*m + 0.2309699292f*s,
        -1.2684380046f*l + 2.6097574011f*m - 0.3413193965f*s,
        -0.0041960863f*l - 0.7034186147f*m + 1.7076147010f*s);
}
bool GenericFiniteV2(float3 c) {
    return all(isfinite(c)) && all(abs(c) <= 65504.0f);
}
float GenericControlLumaV2(float originalY, float candidateY,
    float bright, float dark, float light) {
    // A colour-only control does not perturb neutral luminance by a redundant
    // subtract/add round trip. The same scalar runs at the final pixel and
    // every lighting/detail tap; no Oklab conversion is needed by neighbours.
    if (bright == 1.0f && dark == 1.0f && light == 1.0f) return candidateY;
    float deltaY = candidateY - originalY;
    return originalY +
        (max(deltaY, 0.0f)*bright + min(deltaY, 0.0f)*dark)*light;
}
GenericControlledV2 GenericControlLinearV2(float3 original, float3 answer,
    float total, float bright, float dark, float colour, float light) {
    GenericControlledV2 result;
    result.colorLinear = answer;
    result.lumaLinear = dot(answer, kLuma);
    result.valid = GenericFiniteV2(original) && GenericFiniteV2(answer) &&
        isfinite(total) && isfinite(bright) && isfinite(dark) &&
        isfinite(colour) && isfinite(light);
    if (!result.valid) return result;
    if (total == 1.0f && bright == 1.0f && dark == 1.0f &&
        colour == 1.0f && light == 1.0f) return result;
    float3 delta = answer - original;
    if (total == 0.0f || all(delta == 0.0f)) {
        result.colorLinear = original;
        result.lumaLinear = dot(original, kLuma);
        return result;
    }
    float3 candidate = total == 1.0f ? answer : original + delta*total;
    // Only the scalar crosses the bright/dark branch. The colour vector does
    // not jump when a nonzero chroma edit has zero linear luminance difference.
    result.lumaLinear = GenericControlLumaV2(dot(original, kLuma),
        dot(candidate, kLuma), bright, dark, light);
    result.colorLinear = candidate;
    if (colour == 0.0f) result.colorLinear = original;
    [branch] if (colour != 0.0f && colour != 1.0f) {
        float3 a = GenericLinearToLabV2(original);
        float3 b = GenericLinearToLabV2(candidate);
        float3 mixed = lerp(a, b, colour);
        // Cartesian interpolation has no undefined hue at the grey axis.
        // Endpoint correction cancels matrix round-trip error continuously;
        // it does not clamp signed/over-range FP32 candidates at nonneutral
        // settings. Core still uses the independent lumaLinear signal below.
        float3 correction = lerp(original - GenericLabToLinearV2(a),
            candidate - GenericLabToLinearV2(b), colour);
        result.colorLinear = GenericLabToLinearV2(mixed) + correction;
    }
    result.valid = GenericFiniteV2(result.colorLinear) &&
        isfinite(result.lumaLinear) && abs(result.lumaLinear) <= 65504.0f;
    return result;
}
// NR_GENERIC_CONTROLS_MATH_END
#endif
