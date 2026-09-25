#include "nr_edit_scalar.hlsli"
cbuffer EditConstants:register(b0) {
    uint2 RenderSize;uint2 OutputSize;
    float InputPreExposure;float OutputPreExposure;float NewSampleRate;float DepthAbsoluteTolerance;
    float DepthRelativeTolerance;float StaticMotionTolerancePixels;uint HistoryValid;uint FramePairValid;
    float2 CurrentProjectionAB;float2 PreviousProjectionAB;
    row_major float4x4 CurrentClipToPreviousClip;
    uint CameraMappingVerified;uint ExplicitPreviousDepthVerified;uint ForceIdentity;uint Padding;
};
Texture2D<float4> Edited:register(t0);Texture2D<float4> Original:register(t1);
Texture2D<float2> MotionPixels:register(t2);Texture2D<float> HardwareDepth:register(t3);
Texture2D<float4> PreviousEdit:register(t4);Texture2D<float> PreviousDepth:register(t5);
Texture2D<float4> CleanOutput:register(t6);Texture2D<float4> Reprojection:register(t7);
Texture2D<float4> CurrentEdit:register(t8);Texture2D<float> CurrentDepth:register(t9);
Texture2D<float2> ExplicitPreviousDepth:register(t10);
RWTexture2D<float4> WriteReprojection:register(u0);RWTexture2D<float> WriteDepth:register(u1);
RWTexture2D<float4> WriteEdit:register(u2);RWTexture2D<float> WriteDepthHistory:register(u3);
RWTexture2D<float4> WriteFinal:register(u4);
bool ExposureValid(){return isfinite(InputPreExposure)&&isfinite(OutputPreExposure)&&InputPreExposure>1e-6&&OutputPreExposure>1e-6;}
int2 ClampR(int2 p){return clamp(p,int2(0,0),int2(RenderSize)-1);}
bool Delta(int2 p,out float3 value){float3 a=Edited.Load(int3(p,0)).rgb,b=Original.Load(int3(p,0)).rgb;
    value=(a-b)/InputPreExposure;return all(isfinite(a))&&all(isfinite(b))&&all(isfinite(value));}
[numthreads(8,8,1)]
void PrepareGuidesCS(uint3 id:SV_DispatchThreadID){
    if(any(id.xy>=RenderSize))return;int2 p=int2(id.xy);
    float hardware=HardwareDepth.Load(int3(p,0));
    float depth=NrViewZ(hardware,CurrentProjectionAB.x,CurrentProjectionAB.y);
    // Spatial-only depth metric; never used to authorize temporal history.
    if((Padding&1)!=0)depth=isfinite(hardware)?((Padding&2)!=0?1-hardware:hardware)+1e-6:0;
    WriteDepth[p]=depth;
    float2 uv=(float2(p)+.5)/float2(RenderSize),motion=MotionPixels.Load(int3(p,0));
    float2 previousUv=uv+motion/float2(RenderSize);float4 result=0;
    if(!HistoryValid||!FramePairValid||(Padding&1)!=0||depth<=0||!all(isfinite(motion))||any(previousUv<0)||any(previousUv>1)){
        WriteReprojection[p]=0;return;
    }
    result.xy=previousUv;
    if(ExplicitPreviousDepthVerified){float2 expected=ExplicitPreviousDepth.Load(int3(p,0));
        if(all(isfinite(expected))&&expected.x>0)result.zw=float2(expected.x,saturate(expected.y));
    }else if(CameraMappingVerified){
        float4 clip=mul(CurrentClipToPreviousClip,float4(2*uv.x-1,1-2*uv.y,hardware,1));
        if(all(isfinite(clip))&&clip.w>1e-6){float3 ndc=clip.xyz/clip.w;
            float2 cameraUv=float2(ndc.x*.5+.5,.5-ndc.y*.5);
            float z=NrViewZ(ndc.z,PreviousProjectionAB.x,PreviousProjectionAB.y);
            float mismatch=length((cameraUv-previousUv)*float2(RenderSize));
            if(z>0&&isfinite(mismatch)&&mismatch<=StaticMotionTolerancePixels)result.zw=float2(z,1);
        }
    }
    WriteReprojection[p]=result;
}
[numthreads(8,8,1)]
void AccumulateCS(uint3 id:SV_DispatchThreadID){
    if(any(id.xy>=RenderSize))return;int2 p=int2(id.xy);float z=CurrentDepth.Load(int3(p,0));
    WriteDepthHistory[p]=isfinite(z)&&z>0?z:0;
    float3 delta=0;if(!FramePairValid||ForceIdentity||!ExposureValid()||!Delta(p,delta)){WriteEdit[p]=0;return;}
    float3 lo=delta,hi=delta;
    [unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++){
        float3 v;if(Delta(ClampR(p+int2(x,y)),v)){lo=min(lo,v);hi=max(hi,v);}}
    float3 history=0;float weight=0;
    if(HistoryValid){float4 guide=Reprojection.Load(int3(p,0));
        if(all(isfinite(guide))&&guide.w>0&&all(guide.xy>=0)&&all(guide.xy<=1)){
            float2 pos=guide.xy*float2(RenderSize)-.5,f=frac(pos);int2 origin=int2(floor(pos));
            [unroll]for(int y=0;y<2;y++)[unroll]for(int x=0;x<2;x++){
                int2 q=origin+int2(x,y);if(any(q<0)||any(q>=int2(RenderSize)))continue;
                float4 old=PreviousEdit.Load(int3(q,0));float previousZ=PreviousDepth.Load(int3(q,0));
                float w=(x?f.x:1-f.x)*(y?f.y:1-f.y)*NrDepthWeight(guide.z,previousZ,guide.w,DepthAbsoluteTolerance,DepthRelativeTolerance);
                if(all(isfinite(old))&&old.a>0){history+=old.rgb*w;weight+=w;}
            }
        }
    }
    history=weight>1e-6?history/weight:delta;
    float3 value=NrAccumulate(delta,history,weight,NewSampleRate,lo,hi);
    WriteEdit[p]=all(isfinite(value))?float4(value,1):float4(0,0,0,0);
}
[numthreads(8,8,1)]
void ComposeCS(uint3 id:SV_DispatchThreadID){
    if(any(id.xy>=OutputSize))return;int2 pixel=int2(id.xy);float4 clean=CleanOutput.Load(int3(pixel,0));
    if(!FramePairValid||ForceIdentity||!ExposureValid()){WriteFinal[pixel]=clean;return;}
    float2 p=(float2(pixel)+.5)*float2(RenderSize)/float2(OutputSize)-.5;
    int2 origin=int2(floor(p)),anchor=ClampR(int2(floor(p+.5)));float2 f=frac(p);
    float anchorZ=CurrentDepth.Load(int3(anchor,0));float3 edit=0;float weight=0;
    [unroll]for(int y=0;y<2;y++)[unroll]for(int x=0;x<2;x++){
        int2 q=ClampR(origin+int2(x,y));float4 value=CurrentEdit.Load(int3(q,0));float z=CurrentDepth.Load(int3(q,0));
        float w=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        if(anchorZ>0)w*=NrDepthWeight(anchorZ,z,1,DepthAbsoluteTolerance,DepthRelativeTolerance);else if(z>0)w=0;
        if(all(isfinite(value))&&value.a>0){edit+=value.rgb*w;weight+=w;}
    }
    edit=weight>1e-6?edit/weight:0;float3 rgb=clean.rgb+edit*OutputPreExposure;
    WriteFinal[pixel]=all(isfinite(rgb))&&all(abs(rgb)<=65504)?float4(rgb,clean.a):clean;
}
