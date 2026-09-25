#pragma once
// Appended to the production Present adapters. Both inputs must have the same
// verified value domain and raster. No depth/motion/alpha semantics are inferred.
inline constexpr char kFgHudShader[] = R"HLSL(
Texture2D<float4> OriginalFull : register(t3);
Texture2D<float> OriginalUIAlpha : register(t4);
float4 HudPS(Varying p):SV_Target {
    int2 xy=int2(p.position.xy);
    float4 full=OriginalFull.Load(int3(xy,0));
    float3 scene_raw=Raw.Load(int3(xy,0)).rgb;
    float3 scene=Canonical.Load(int3(xy,0)).rgb;
    float3 edited=Composed.Load(int3(xy,0)).rgb;
    // With an explicit alpha contract, keep the original composited UI and
    // update only its uncovered scene contribution. Otherwise protect every
    // pixel where the original HUDless/full pair differs; this is a conservative
    // protection mask, deliberately NOT called recovered native UI alpha.
    float coverage=(Padding&1u)?saturate(OriginalUIAlpha.Load(int3(xy,0))):
        (any(full.rgb!=scene_raw)?1.0:0.0);
    if(!isfinite(coverage)||coverage>=1.0||!Finite(edited)||!Finite(scene)||all(edited==scene))return full;
    float3 base=full.rgb;
    if(Encoding==1)base=ToLinear(saturate(base));
    if(Encoding==3)base=From2020(FromPq(base)*125.0);
    float3 c=base+(edited-scene)*(1.0-coverage);
    if(!Finite(c))return full;
    if(Encoding==1)c=saturate(ToSrgb(c));
    if(Encoding==3)c=ToPq(max(To2020(c),0)/125.0);
    return all(isfinite(c))?float4(c,full.a):full;
}
)HLSL";
