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
Texture2D<float> CurrentDepth : register(t9);
Texture2D<float> PreviousDepth : register(t10);
RWTexture2D<float4> Snapshot : register(u2);
RWTexture2D<float4> NextOriginal : register(u3);
RWTexture2D<float> NextDepth : register(u4);
RWByteAddressBuffer HistoryStats : register(u5);
#endif
RWTexture2D<float4> NextResidual : register(u0);
RWTexture2D<float4> Output : register(u1);
SamplerState LinearClamp : register(s0);
cbuffer Params : register(b0) { uint Width,Height,Accumulate,LaterLayers; float Weight,ColorTolerance;
#ifdef RECORDING
    uint FlowWidth,FlowHeight,FlowX,FlowY; float UnitX,UnitY;
    uint IsAmd,FlowAge,TargetX,TargetY;
    uint LinearHdr,TargetKind; float SourceToScene,PreviousToScene;
    uint ValidationMode,TemporalMode,DepthEnabled,StatsEnabled;
#endif
};
#ifdef RECORDING
[numthreads(8,8,1)] void CopyCandidate(uint3 id:SV_DispatchThreadID) {
    if(id.x<Width&&id.y<Height)Snapshot[id.xy]=CurrentTarget.Load(int3(id.xy+uint2(TargetX,TargetY),0));
}
#endif
#ifdef RECORDING
float Magnitude(float3 v) { return max(abs(v.x),max(abs(v.y),abs(v.z))); }
float3 MatchColor(float3 v,float scale) {
    if(LinearHdr!=0){v*=scale;v=v/(1+abs(v));}return v;
}
bool NeighborhoodResidual(int2 q,out float3 edit,out float3 source) {
    q=clamp(q,int2(0,0),int2(Width-1,Height-1));
    source=Original.Load(int3(q,0)).rgb;
    float3 base=LaterLayers!=0?Anchor.Load(int3(q,0)).rgb:source;
    float3 value=Enhanced.Load(int3(q,0)).rgb;
    bool valid=all(isfinite(source))&&all(isfinite(base))&&all(isfinite(value));
    if(LinearHdr==0)valid=valid&&all(source>=0)&&all(source<=1)&&all(base>=0)&&all(base<=1)&&all(value>=0)&&all(value<=1);
    edit=(value-base)*(LinearHdr!=0?SourceToScene:1);
    return valid&&all(isfinite(edit))&&all(abs(edit)<=65504);
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
#ifdef RECORDING
    float3 low=current,bounds_min=current,bounds_max=current;
    // Basic modes read an immutable snapshot. The opt-in in-place variant is
    // Legacy only; no neighboring UAV reads race with the current writeback.
#ifndef INPLACE_FP16
    if(ValidationMode==0&&current_valid){
        float3 sum=0;float total=0;
        const float3 center_match=MatchColor(original.rgb,SourceToScene);
        [unroll]for(int dy=-1;dy<=1;++dy)[unroll]for(int dx=-1;dx<=1;++dx){
            float3 edit,source;
            if(NeighborhoodResidual(int2(p)+int2(dx,dy),edit,source)){
                const float distance=Magnitude(MatchColor(source,SourceToScene)-center_match);
                const float edge=saturate(1-distance/max(2*ColorTolerance,1e-5));
                const float spatial=(dx==0?2:1)*(dy==0?2:1);
                if(edge>0){
                    bounds_min=min(bounds_min,edit);bounds_max=max(bounds_max,edit);
                    sum+=edit*(edge*spatial);total+=edge*spatial;
                }
            }
        }
        if(total>0)low=sum/total;
    }
#endif
    const float3 signal=ValidationMode==0&&TemporalMode==1?low:current;
    residual=signal;
    bool history_used=false,depth_rejected=false;
    uint2 depth_q=min(uint2((float2(p)+.5)*float2(FlowWidth,FlowHeight)/float2(Width,Height)),uint2(max(FlowWidth,1u)-1,max(FlowHeight,1u)-1));
    float depth=DepthEnabled!=0?CurrentDepth.Load(int3(depth_q,0)):0;
    if(DepthEnabled!=0)NextDepth[p]=depth;
#endif
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
            bool accepted=all(isfinite(previous_residual))&&all(isfinite(previous_match))&&all(abs(current_match-previous_match)<=ColorTolerance);
#ifdef RECORDING
            if(DepthEnabled!=0){
                // Point samples preserve the surface decision at depth edges.
                // This is conservative hardware-depth disagreement rejection,
                // not a reconstructed previous-view position or OF confidence.
                const int2 previous_p=int2(clamp(floor(reference+.5),0,float2(Width-1,Height-1)));
                const float old_depth=PreviousDepth.Load(int3(previous_p,0));
                const float tolerance=max(1e-4,.01*min(abs(depth),abs(old_depth)));
                const bool matches=isfinite(depth)&&isfinite(old_depth)&&depth>=0&&depth<=1&&old_depth>=0&&old_depth<=1&&abs(depth-old_depth)<=tolerance;
                depth_rejected=!matches;accepted=accepted&&matches;
            }
            if(accepted){
                float effective=Weight;float3 constrained=previous_residual;
                if(ValidationMode==0){
                    const float3 margin=max(.002,abs(signal)*.25+(bounds_max-bounds_min)*.5);
                    constrained=clamp(previous_residual,bounds_min-margin,bounds_max+margin);
                    const float color_agreement=saturate(1-Magnitude(current_match-previous_match)/max(ColorTolerance,1e-5));
                    const float edit_difference=Magnitude(constrained-signal);
                    const float edit_scale=Magnitude(constrained)+Magnitude(signal)+.01;
                    const float edit_agreement=saturate(1-edit_difference/edit_scale);
                    const bool opposite=dot(constrained,signal)<-1e-6&&Magnitude(constrained)>.002&&Magnitude(signal)>.002;
                    effective*=color_agreement*edit_agreement*(opposite?0:1);
                }
                history_used=effective>1e-6;
                residual=lerp(signal,constrained,effective);
            }
#else
            if(accepted)residual=lerp(current,previous_residual,Weight);
#endif
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
    float3 final_residual=residual;
#ifdef RECORDING
    // Store low-band history separately while preserving current high frequency.
    if(ValidationMode==0&&TemporalMode==1)final_residual+=(current-low);
#endif
    float4 result=float4(current_valid?saturate(base+final_residual):fallback,isfinite(original.a)?original.a:1);
#ifdef RECORDING
    if(LinearHdr!=0){
        float3 restored=base+final_residual/SourceToScene;
        bool fits=all(isfinite(restored))&&all(abs(restored)<=65504);
        if((TargetKind&255u)==2)fits=fits&&all(restored>=0)&&all(restored<=float3(65024,65024,64512));
        result.rgb=current_valid&&fits?restored:fallback;
        if(!fits){residual=0;history_used=false;}
    }
#endif
#ifdef RECORDING
    // One actual shader decision per 8x8 tile. No full-frame atomic reduction,
    // no CPU wait and no fabricated per-pixel optical-flow confidence.
    if(StatsEnabled!=0&&(p.x&7u)==0&&(p.y&7u)==0){
        uint ignored;
        if(!motion_ready)HistoryStats.InterlockedAdd(12,1,ignored);
        else{
            HistoryStats.InterlockedAdd(0,1,ignored);
            HistoryStats.InterlockedAdd(history_used?4:8,1,ignored);
            if(depth_rejected)HistoryStats.InterlockedAdd(16,1,ignored);
        }
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
