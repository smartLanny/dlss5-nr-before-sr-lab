#pragma once
namespace nr::final_residual {
// Independently authored. Original input drives rejection, never enhanced RGB.
inline constexpr char residual_accumulator_shader[]=R"HLSL(
Texture2D<float4> Original : register(t0);
Texture2D<float4> Enhanced : register(t1);
Texture2D<float2> Flow : register(t2);
Texture2D<float4> PreviousOriginal : register(t3);
Texture2D<float4> PreviousResidual : register(t4);
RWTexture2D<float4> NextResidual : register(u0);
RWTexture2D<float4> Output : register(u1);
SamplerState LinearClamp : register(s0);
cbuffer Params : register(b0) { uint Width,Height,Accumulate,Reserved; float Weight,ColorTolerance; };
[numthreads(8,8,1)] void CS(uint3 id:SV_DispatchThreadID) {
    uint2 p=id.xy; if(p.x>=Width||p.y>=Height)return;
    float4 original=Original.Load(int3(p,0));
    float3 enhanced=Enhanced.Load(int3(p,0)).rgb;
    // Invalid/out-of-domain enhancement cannot poison an FP16 history.
    bool current_valid=all(isfinite(enhanced))&&all(enhanced>=0)&&all(enhanced<=1);
    float3 current=current_valid?enhanced-original.rgb:0;
    float3 residual=current;
    if(Accumulate!=0&&current_valid) {
        float2 motion=Flow.Load(int3(p,0));
        float2 reference=float2(p)+motion;
        bool valid=all(isfinite(motion))&&all(reference>=0)&&all(reference<=float2(Width-1,Height-1));
        if(valid) {
            float2 uv=(reference+0.5)/float2(Width,Height);
            float3 previous_original=PreviousOriginal.SampleLevel(LinearClamp,uv,0).rgb;
            float3 previous_residual=PreviousResidual.SampleLevel(LinearClamp,uv,0).rgb;
            // Photometric rejection plus out-of-bounds disocclusion proxy.
            // No claim of depth/forward-backward optical-flow confidence.
            if(all(isfinite(previous_residual))&&all(abs(original.rgb-previous_original)<=ColorTolerance))
                residual=lerp(current,previous_residual,Weight);
        }
    }
    NextResidual[p]=float4(residual,0); // signed, never saturate history
    Output[p]=float4(saturate(original.rgb+residual),original.a);
}
)HLSL";
}
