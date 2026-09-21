#include "nr_edit_scalar.hlsli"
cbuffer DeferredConstants:register(b0){
    uint2 RenderSize;uint2 OutputSize;
    float InputPreExposure;float OutputPreExposure;float CarrierRange;uint FramePairValid;
    uint PrivateSrSucceeded;uint ForceIdentity;uint Padding0;uint Padding1;
};
Texture2D<float4> Edited:register(t0);Texture2D<float4> Original:register(t1);
Texture2D<float4> UpscaledCarrier:register(t2);Texture2D<float4> Tail:register(t3);
Texture2D<float4> CleanOutput:register(t4);
RWTexture2D<float4> WriteCarrier:register(u0);RWTexture2D<float4> WriteTail:register(u1);
RWTexture2D<float4> WriteFinal:register(u2);
bool Valid(){return all(RenderSize>0)&&all(OutputSize>0)&&isfinite(InputPreExposure)&&isfinite(OutputPreExposure)&&
    InputPreExposure>1e-6&&OutputPreExposure>1e-6&&isfinite(CarrierRange)&&CarrierRange>1e-6&&CarrierRange<=1e6;}
float NearestCarrierHalf(float value){
    // f32tof16 need not provide the rounding used by an FP16 UAV store. Choose
    // the nearest representable nonnegative value explicitly (ties to even),
    // so tiny positive/negative edits have symmetric quantization near 0.5.
    uint low=f32tof16(value);
    if(low>0&&f16tof32(low)>value)--low;
    float a=f16tof32(low),b=f16tof32(low+1);
    float da=value-a,db=b-value;
    return db<da||(db==da&&(low&1))?b:a;
}
[numthreads(8,8,1)]
void EncodeCS(uint3 id:SV_DispatchThreadID){
    if(any(id.xy>=RenderSize))return;int2 p=int2(id.xy);
    float3 e=Edited.Load(int3(p,0)).rgb,o=Original.Load(int3(p,0)).rgb,d=(e-o)/InputPreExposure;
    bool valid=FramePairValid&&!ForceIdentity&&Valid()&&all(isfinite(e))&&all(isfinite(o))&&all(isfinite(d));
    float3 bounded=clamp(d,-CarrierRange,CarrierRange);
    // Tail must account for the value the FP16 texture actually stores,
    // including tiny signed edits rounded away near the neutral 0.5 carrier.
    // The explicit round trip is idempotent when written into RGBA16F.
    float3 encoded=.5+.5*bounded/CarrierRange;
    float3 stored=float3(NearestCarrierHalf(encoded.x),NearestCarrierHalf(encoded.y),NearestCarrierHalf(encoded.z));
    float3 represented=(2*stored-1)*CarrierRange;
    WriteCarrier[p]=valid?float4(stored,1):float4(.5,.5,.5,1);
    // Actual DLSS can move a perfectly neutral 0.5 carrier. Preserve an exact
    // no-SR-contribution tag when FP16 encoded no edit. Tiny signed changes
    // still survive in the FP32 tail; this is not an epsilon/dead-zone filter.
    WriteTail[p]=valid?float4(d-represented,any(represented!=0)?2:1):float4(0,0,0,0);
}
[numthreads(8,8,1)]
void ComposeCS(uint3 id:SV_DispatchThreadID){
    if(any(id.xy>=OutputSize))return;int2 p=int2(id.xy);float4 clean=CleanOutput.Load(int3(p,0));
    if(!FramePairValid||!PrivateSrSucceeded||ForceIdentity||!Valid()){WriteFinal[p]=clean;return;}
    float4 carrier=UpscaledCarrier.Load(int3(p,0));if(!all(isfinite(carrier))){WriteFinal[p]=clean;return;}
    float2 pos=(float2(p)+.5)*float2(RenderSize)/float2(OutputSize)-.5,f=frac(pos);int2 origin=int2(floor(pos));
    float3 tail=0;float total=0;bool hasCarrierEdit=false;
    [unroll]for(int y=0;y<2;y++)[unroll]for(int x=0;x<2;x++){
        int2 q=clamp(origin+int2(x,y),int2(0,0),int2(RenderSize)-1);float4 value=Tail.Load(int3(q,0));
        float w=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        if(w>0&&(!all(isfinite(value))||value.a<.5)){WriteFinal[p]=clean;return;}
        hasCarrierEdit=hasCarrierEdit||(w>0&&value.a>=1.5);
        tail+=value.rgb*w;total+=w;
    }
    if(total<=0){WriteFinal[p]=clean;return;}
    float3 delta=(hasCarrierEdit?(2*saturate(carrier.rgb)-1)*CarrierRange:float3(0,0,0))+tail/total;
    float3 rgb=clean.rgb+delta*OutputPreExposure;
    WriteFinal[p]=all(isfinite(rgb))&&all(abs(rgb)<=65504)?float4(rgb,clean.a):clean;
}
