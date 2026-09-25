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
RWTexture2D<float4> NextOriginal : register(u3);
#endif
RWTexture2D<float4> NextResidual : register(u0);
RWTexture2D<float4> Output : register(u1);
SamplerState LinearClamp : register(s0);
cbuffer Params : register(b0) { uint Width,Height,Accumulate,LaterLayers; float Weight,ColorTolerance;
#ifdef RECORDING
    uint FlowWidth,FlowHeight,FlowX,FlowY; float UnitX,UnitY;
    uint IsAmd,FlowAge,TargetX,TargetY;
    uint LinearHdr,TargetKind; float SourceToScene,PreviousToScene;
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
#if defined(RECORDING) && defined(INPLACE_FP16)
    // Opt-in only: typed FP16 UAV load, one unique pixel per thread. No current
    // neighbor reads; the two reprojected histories are different resources.
    float3 enhanced=Output.Load(p+uint2(TargetX,TargetY)).rgb;
#else
    float3 enhanced=Enhanced.Load(int3(p,0)).rgb;
#endif
    float3 base=LaterLayers!=0?Anchor.Load(int3(p,0)).rgb:original.rgb;
    // Invalid/out-of-domain enhancement cannot poison an FP16 history.
    bool current_valid=all(isfinite(enhanced))&&all(enhanced>=0)&&all(enhanced<=1)&&
        all(isfinite(base))&&all(base>=0)&&all(base<=1);
    bool original_valid=all(isfinite(original.rgb))&&all(original.rgb>=0)&&all(original.rgb<=1);
#ifdef RECORDING
    if(LinearHdr!=0){
        current_valid=all(isfinite(enhanced))&&all(isfinite(base));
        original_valid=all(isfinite(original.rgb));
    }
#endif
    current_valid=current_valid&&original_valid;
    float3 current=current_valid?enhanced-base:0;
#ifdef RECORDING
    if(LinearHdr!=0){
        current*=SourceToScene;
        current_valid=current_valid&&all(isfinite(current))&&all(abs(current)<=65504);
        if(!current_valid)current=0;
    }
#endif
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
            float3 current_match=original.rgb,previous_match=previous_original;
#ifdef RECORDING
            if(LinearHdr!=0){
                current_match*=SourceToScene;previous_match*=PreviousToScene;
                current_match=current_match/(1+abs(current_match));previous_match=previous_match/(1+abs(previous_match));
            }
#endif
            if(all(isfinite(previous_residual))&&all(isfinite(previous_match))&&all(abs(current_match-previous_match)<=ColorTolerance))
                residual=lerp(current,previous_residual,Weight);
        }
    }
    // Invalid P1 must not discard a valid whole-chain Z. Neither invalid B
    // nor an invalid model pixel can seed nonfinite signed history.
    // Domain rejection means no temporal edit, not clipping a finite current
    // NR result into SDR. Preserve overshoots/undershoots in a float target.
#ifdef RECORDING
    float3 fallback=all(isfinite(enhanced))?enhanced:all(isfinite(original.rgb))?original.rgb:0;
#else
    // Keep the standalone legacy bounded-SDR oracle unchanged. The borrowed
    // Core path above preserves a finite pre-existing candidate on refusal.
    float3 fallback=original_valid?original.rgb:0;
#endif
    float4 result=float4(current_valid?saturate(base+residual):fallback,isfinite(original.a)?original.a:1);
#ifdef RECORDING
    if(LinearHdr!=0){
        float3 restored=base+residual/SourceToScene;
        bool fits=all(isfinite(restored))&&all(abs(restored)<=65504);
        if((TargetKind&255u)==2)fits=fits&&all(restored>=0)&&all(restored<=float3(65024,65024,64512));
        result.rgb=current_valid&&fits?restored:fallback;
        if(!fits)residual=0;
    }
#endif
    NextResidual[p]=float4(residual,0); // signed scene/code residual, never saturated
#ifdef RECORDING
    // Packed or padded float source is converted into the already-owned history slot.
    // No extra full-resolution texture or dispatch. Exact-size FP16 retains CopyResource.
    if((TargetKind&256u)!=0)NextOriginal[p]=original;
    Output[p+uint2(TargetX,TargetY)]=result;
#else
    Output[p]=result;
#endif
}
)HLSL";
}
