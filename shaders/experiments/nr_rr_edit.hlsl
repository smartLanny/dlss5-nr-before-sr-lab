// RR edit carry prototype. These entry points are NOT yet wired into D16.
// All colour inputs are canonical scene-linear in the SAME exposure domain.
// Native RR Color is never a UAV here. Only private scratch/output textures are writable.
#include "nr_edit_scalar.hlsli"
cbuffer EditConstants : register(b0) {
    uint2 RenderSize;
    uint2 OutputSize;
    float InputPreExposure;
    float OutputPreExposure;
    float NewSampleRate;
    float DepthAbsoluteTolerance;
    float DepthRelativeTolerance;
    float StaticMotionTolerancePixels;
    uint HistoryValid;
    uint FramePairValid;
    float2 CurrentProjectionAB;
    float2 PreviousProjectionAB;
    row_major float4x4 CurrentClipToPreviousClip;
    uint CameraMappingVerified;
    uint ExplicitPreviousDepthVerified;
    uint ForceIdentity;
    uint Padding;
};
Texture2D<float4> Edited : register(t0);
Texture2D<float4> Original : register(t1);
Texture2D<float2> MotionPixels : register(t2);
Texture2D<float> CurrentHardwareDepth : register(t3);
Texture2D<float4> PreviousEdit : register(t4);
Texture2D<float> PreviousViewDepth : register(t5);
Texture2D<float4> CleanRrOutput : register(t6);
Texture2D<float4> Reprojection : register(t7);
Texture2D<float4> CurrentEdit : register(t8);
Texture2D<float> CurrentViewDepth : register(t9);
// Optional true per-pixel expected depth in the previous camera + confidence.
// Must come from a verified provider; camera-only reconstruction is NOT object motion.
Texture2D<float2> ExplicitPreviousDepth : register(t10);
RWTexture2D<float4> WriteReprojection : register(u0);
RWTexture2D<float> WriteViewDepth : register(u1);
RWTexture2D<float4> WriteEdit : register(u2);
RWTexture2D<float> WriteDepthHistory : register(u3);
RWTexture2D<float4> WriteFinal : register(u4);

bool ExposureValid() {
    return isfinite(InputPreExposure)&&isfinite(OutputPreExposure)&&
        InputPreExposure>1e-6&&OutputPreExposure>1e-6;
}
int2 ClampRender(int2 p) {return clamp(p,int2(0,0),int2(RenderSize)-1);}
bool CurrentDelta(int2 p,out float3 d) {
    float3 e=Edited.Load(int3(p,0)).rgb;
    float3 b=Original.Load(int3(p,0)).rgb;
    d=(e-b)/InputPreExposure;
    return all(isfinite(d))&&all(isfinite(e))&&all(isfinite(b));
}
// Prepare owned, zero-origin R-sized guides. The adapter normalizes native
// active rect, depth convention, MV direction/units and jitter/matrix phases.
[numthreads(8,8,1)]
void PrepareGuidesCS(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=RenderSize))return;
    int2 p=int2(id.xy);
    float depth=CurrentHardwareDepth.Load(int3(p,0));
    float currentZ=NrEditViewZ(depth,CurrentProjectionAB.x,CurrentProjectionAB.y);
    WriteViewDepth[p]=currentZ;
    float2 uv=(float2(p)+0.5)/float2(RenderSize);
    float2 motion=MotionPixels.Load(int3(p,0));
    float2 previousUv=uv+motion/float2(RenderSize);
    float4 result=float4(previousUv,0,0);
    if(!FramePairValid||!HistoryValid||currentZ<=0||!all(isfinite(motion))||
       any(previousUv<0)||any(previousUv>1)) {WriteReprojection[p]=float4(0,0,0,0);return;}
    if(ExplicitPreviousDepthVerified) {
        float2 expected=ExplicitPreviousDepth.Load(int3(p,0));
        if(all(isfinite(expected))&&expected.x>0)
            result.zw=float2(expected.x,saturate(expected.y));
    } else if(CameraMappingVerified) {
        // Column-vector convention. Matrix includes the actual raster phase.
        float4 previousClip=mul(CurrentClipToPreviousClip,float4(2*uv.x-1,1-2*uv.y,depth,1));
        if(all(isfinite(previousClip))&&previousClip.w>1e-6) {
            float3 ndc=previousClip.xyz/previousClip.w;
            float2 cameraUv=float2(ndc.x*0.5+0.5,0.5-ndc.y*0.5);
            float expectedZ=NrEditViewZ(ndc.z,PreviousProjectionAB.x,PreviousProjectionAB.y);
            float mismatch=length((cameraUv-previousUv)*float2(RenderSize));
            // Reject independently moving geometry when only static-camera
            // reprojection can predict previous depth. Do not invent object velocity.
            if(expectedZ>0&&isfinite(mismatch)&&isfinite(StaticMotionTolerancePixels)&&
               StaticMotionTolerancePixels>=0&&mismatch<=StaticMotionTolerancePixels)
                result.zw=float2(expectedZ,1);
        }
    }
    WriteReprojection[p]=result;
}
[numthreads(8,8,1)]
void AccumulateCS(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=RenderSize))return;
    int2 p=int2(id.xy);float3 delta=0;
    float z=CurrentViewDepth.Load(int3(p,0));
    WriteDepthHistory[p]=isfinite(z)&&z>0?z:0;
    if(!FramePairValid||ForceIdentity||!ExposureValid()||!CurrentDelta(p,delta)) {
        WriteEdit[p]=float4(0,0,0,0);return;
    }
    float3 lo=delta,hi=delta;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
        float3 n;
        if(CurrentDelta(ClampRender(p+int2(x,y)),n)){lo=min(lo,n);hi=max(hi,n);}
    }
    float3 history=0;float weight=0;
    // On a cold frame this branch must not read an uninitialized history allocation.
    if(HistoryValid) {
        float4 guide=Reprojection.Load(int3(p,0));
        if(all(isfinite(guide))&&guide.w>0&&all(guide.xy>=0)&&all(guide.xy<=1)) {
            float2 samplePos=guide.xy*float2(RenderSize)-0.5;
            int2 origin=int2(floor(samplePos));float2 f=frac(samplePos);
            [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
                int2 q=origin+int2(x,y);
                if(any(q<0)||any(q>=int2(RenderSize)))continue;
                float4 old=PreviousEdit.Load(int3(q,0));
                float previousZ=PreviousViewDepth.Load(int3(q,0));
                float w=(x?f.x:1-f.x)*(y?f.y:1-f.y);
                w*=NrEditDepthWeight(guide.z,previousZ,guide.w,
                                    DepthAbsoluteTolerance,DepthRelativeTolerance);
                if(all(isfinite(old))&&old.a>0){history+=old.rgb*w;weight+=w;}
            }
        }
    }
    if(weight>1e-6)history/=weight;else history=delta;
    float3 value;
    value.r=NrEditAccumulate(delta.r,history.r,weight,NewSampleRate,lo.r,hi.r);
    value.g=NrEditAccumulate(delta.g,history.g,weight,NewSampleRate,lo.g,hi.g);
    value.b=NrEditAccumulate(delta.b,history.b,weight,NewSampleRate,lo.b,hi.b);
    WriteEdit[p]=all(isfinite(value))?float4(value,1):float4(0,0,0,0);
}
[numthreads(8,8,1)]
void ComposeCS(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=OutputSize))return;
    int2 outputPixel=int2(id.xy);
    float4 clean=CleanRrOutput.Load(int3(outputPixel,0));
    if(!FramePairValid||ForceIdentity||!ExposureValid()) {WriteFinal[outputPixel]=clean;return;}
    float2 p=(float2(outputPixel)+0.5)*float2(RenderSize)/float2(OutputSize)-0.5;
    int2 origin=int2(floor(p));float2 f=frac(p);
    int2 anchor=ClampRender(int2(floor(p+0.5)));
    float anchorZ=CurrentViewDepth.Load(int3(anchor,0));
    float3 delta=0;float weight=0;
    [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
        int2 q=ClampRender(origin+int2(x,y));
        float4 e=CurrentEdit.Load(int3(q,0));float z=CurrentViewDepth.Load(int3(q,0));
        float w=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        // Same current raster depth is used for spatial upsampling, not for
        // temporal disocclusion. Invalid sky depth never authorizes history.
        if(anchorZ>0)w*=NrEditDepthWeight(anchorZ,z,1,DepthAbsoluteTolerance,DepthRelativeTolerance);
        else if(z>0)w=0;
        if(all(isfinite(e))&&e.a>0){delta+=e.rgb*w;weight+=w;}
    }
    if(weight>1e-6)delta/=weight;else delta=0;
    float3 rgb=clean.rgb+delta*OutputPreExposure;
    // Preserve signed scene-linear colour and alpha. Do not tone-map again.
    // The adapter must apply the existing Core's final headroom/gamut policy once.
    WriteFinal[outputPixel]=all(isfinite(rgb))?float4(rgb,clean.a):clean;
}
