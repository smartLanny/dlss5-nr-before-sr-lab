// Deferred edit carrier prototype: a private DLSS SR feature must process
// Carrier_R -> Carrier_O between EncodeCS and ComposeCS. This shader is NOT SR.
#include "nr_edit_scalar.hlsli"
cbuffer DeferredConstants : register(b0) {
    uint2 RenderSize;
    uint2 OutputSize;
    float InputPreExposure;
    float OutputPreExposure;
    float CarrierRange;
    uint FramePairValid;
    uint PrivateSrSucceeded;
    uint ForceIdentity;
    uint Padding0;
    uint Padding1;
};
Texture2D<float4> EditedRender : register(t0);
Texture2D<float4> OriginalRender : register(t1);
Texture2D<float4> UpscaledCarrier : register(t2);
Texture2D<float4> RenderTail : register(t3);
Texture2D<float4> CleanMainOutput : register(t4);
RWTexture2D<float4> WriteCarrier : register(u0);
RWTexture2D<float4> WriteTail : register(u1);
RWTexture2D<float4> WriteFinal : register(u2);
bool ValidConstants() {
    return all(RenderSize>0)&&all(OutputSize>0)&&isfinite(InputPreExposure)&&
        isfinite(OutputPreExposure)&&InputPreExposure>1e-6&&OutputPreExposure>1e-6&&
        isfinite(CarrierRange)&&CarrierRange>1e-6&&CarrierRange<=1e6;
}
[numthreads(8,8,1)]
void EncodeCS(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=RenderSize))return;
    int2 p=int2(id.xy);
    if(!FramePairValid||ForceIdentity||!ValidConstants()) {
        WriteCarrier[p]=float4(0.5,0.5,0.5,1);WriteTail[p]=float4(0,0,0,0);return;
    }
    float3 edited=EditedRender.Load(int3(p,0)).rgb;
    float3 original=OriginalRender.Load(int3(p,0)).rgb;
    float3 d=(edited-original)/InputPreExposure;
    NrEditPacked r=NrEditPack(d.r,CarrierRange);
    NrEditPacked g=NrEditPack(d.g,CarrierRange);
    NrEditPacked b=NrEditPack(d.b,CarrierRange);
    bool valid=r.valid&&g.valid&&b.valid&&all(isfinite(edited))&&all(isfinite(original));
    WriteCarrier[p]=valid?float4(r.carrier,g.carrier,b.carrier,1):float4(0.5,0.5,0.5,1);
    // FP32 tail + validity. Native SR receives neither of these textures.
    WriteTail[p]=valid?float4(r.tail,g.tail,b.tail,1):float4(0,0,0,0);
}
[numthreads(8,8,1)]
void ComposeCS(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=OutputSize))return;
    int2 p=int2(id.xy);float4 clean=CleanMainOutput.Load(int3(p,0));
    if(!FramePairValid||!PrivateSrSucceeded||ForceIdentity||!ValidConstants()) {WriteFinal[p]=clean;return;}
    float4 carrier=UpscaledCarrier.Load(int3(p,0));
    float2 samplePos=(float2(p)+0.5)*float2(RenderSize)/float2(OutputSize)-0.5;
    int2 origin=int2(floor(samplePos));float2 f=frac(samplePos);
    float3 tail=0;float total=0;
    [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
        int2 q=clamp(origin+int2(x,y),int2(0,0),int2(RenderSize)-1);
        float4 tailSample=RenderTail.Load(int3(q,0));
        float w=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        if(w>0&&(!all(isfinite(tailSample))||tailSample.a<0.5)){WriteFinal[p]=clean;return;}
        tail+=tailSample.rgb*w;total+=w;
    }
    if(total<=0||!all(isfinite(carrier))){WriteFinal[p]=clean;return;}
    tail/=total;
    float3 delta=float3(NrEditUnpack(carrier.r,tail.r,CarrierRange),
                        NrEditUnpack(carrier.g,tail.g,CarrierRange),
                        NrEditUnpack(carrier.b,tail.b,CarrierRange));
    float3 rgb=clean.rgb+delta*OutputPreExposure;
    WriteFinal[p]=all(isfinite(rgb))?float4(rgb,clean.a):clean;
}
// Changing CarrierRange/history domain requires a new private-SR history.
// A successful Encode/Compose dispatch does not prove SR ran or GPU work completed.
// Tail is spatial and can soften high-range edits; this is a measured tradeoff,
// not a claim of matching NR at output resolution.
