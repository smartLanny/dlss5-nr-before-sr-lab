#pragma once
namespace nr::final_residual {
// Independently authored. Original input drives rejection, never enhanced RGB.
inline constexpr char residual_accumulator_shader[]=R"HLSL(
Texture2D<float4> Original : register(t0);
Texture2D<float4> Enhanced : register(t1);
Texture2D<float2> Flow : register(t2);
Texture2D<float4> PreviousOriginal : register(t3);
Texture2D<float4> PreviousResidual : register(t4);
Texture2D<float4> Anchor : register(t5);
#ifdef RECORDING
Texture2D<int2> RawFlow : register(t6);
Texture2D<uint> SceneData : register(t7);
Texture2D<float4> CurrentTarget : register(t8);
RWTexture2D<float4> Snapshot : register(u2);
#endif
RWTexture2D<float4> NextResidual : register(u0);
RWTexture2D<float4> Output : register(u1);
SamplerState LinearClamp : register(s0);
cbuffer Params : register(b0) { uint Width,Height,Accumulate,LaterLayers; float Weight,ColorTolerance;
#ifdef RECORDING
    uint FlowWidth,FlowHeight,FlowX,FlowY; float UnitX,UnitY;
    uint IsAmd,FlowAge,TargetX,TargetY;
#endif
};
#ifdef RECORDING
[numthreads(8,8,1)] void CopyCandidate(uint3 id:SV_DispatchThreadID) {
    if(id.x<Width&&id.y<Height)Snapshot[id.xy]=CurrentTarget.Load(int3(id.xy+uint2(TargetX,TargetY),0));
}
#endif
[numthreads(8,8,1)] void CS(uint3 id:SV_DispatchThreadID) {
    uint2 p=id.xy; if(p.x>=Width||p.y>=Height)return;
    float4 original=Original.Load(int3(p,0));
    float3 enhanced=Enhanced.Load(int3(p,0)).rgb;
    float3 base=LaterLayers!=0?Anchor.Load(int3(p,0)).rgb:original.rgb;
    // Invalid/out-of-domain enhancement cannot poison an FP16 history.
    bool current_valid=all(isfinite(enhanced))&&all(enhanced>=0)&&all(enhanced<=1)&&
        all(isfinite(base))&&all(base>=0)&&all(base<=1);
    float3 current=current_valid?enhanced-base:0;
    float3 residual=current;
    bool motion_ready=Accumulate!=0;
#ifdef RECORDING
    // SDK SCD/warmup stays on the GPU; no current-frame CPU readback.
    if(IsAmd!=0)motion_ready=motion_ready&&FlowAge>5&&
        (SceneData.Load(int3(1,0,0))&15u)==0&&isfinite(asfloat(SceneData.Load(int3(0,0,0))));
#endif
    if(motion_ready&&current_valid) {
#ifdef RECORDING
        uint2 q=min(uint2((float2(p)+0.5)*float2(FlowWidth,FlowHeight)/float2(Width,Height)),uint2(FlowWidth-1,FlowHeight-1));
        float2 motion=IsAmd!=0?float2(RawFlow.Load(int3(q/8,0)))*float2(UnitX,UnitY):
            Flow.Load(int3(q+uint2(FlowX,FlowY),0))*float2(UnitX,UnitY);
#else
        float2 motion=Flow.Load(int3(p,0));
#endif
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
#ifdef RECORDING
    Output[p+uint2(TargetX,TargetY)]=float4(current_valid?saturate(base+residual):original.rgb,original.a);
#else
    Output[p]=float4(current_valid?saturate(base+residual):original.rgb,original.a);
#endif
}
)HLSL";
}
