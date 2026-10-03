#include "nr_edit_scalar.hlsli"
#include "nr_edit_carrier.hlsli"
cbuffer PostConstants:register(b0){
    uint2 FullSize;uint2 WorkSize;
    uint2 BaseOrigin;uint2 CandidateOrigin;
    uint2 TargetOrigin;uint2 ReservedOrigin;
    float CarrierRange;uint PairValid;uint ReconstructionSucceeded;uint Reserved;
};
Texture2D<float4> Candidate:register(t0);
Texture2D<float4> Base:register(t1);
Texture2D<float4> Reconstructed:register(t2);
Texture2D<float4> Tail:register(t3);
RWTexture2D<float4> CarrierOut:register(u0);
RWTexture2D<float4> TailOut:register(u1);
RWTexture2D<float4> Target:register(u2);
bool Valid(){return PairValid&&all(FullSize>0)&&all(WorkSize>0)&&all(WorkSize<=FullSize)&&
    all(WorkSize>=max(uint2(1,1),FullSize/4))&&isfinite(CarrierRange)&&CarrierRange>1e-6&&CarrierRange<=1e6;}
void EncodePostResidualCarrier(float3 delta,bool valid,float range,out float4 carrier,out float4 tail){
    // Post-private-SR only: preserve the old range-4 tail-only contract for
    // each channel that the established encoder would round to neutral.
    // RR/deferred continue to use the unchanged shared encoder.
    EncodeResidualCarrier(delta,valid,range,carrier,tail);
    if(!valid||range>=4)return;
    float3 oldEncoded=.5+.5*clamp(delta,-4,4)/4;
    float3 oldStored=float3(NearestCarrierHalf(oldEncoded.x),
                            NearestCarrierHalf(oldEncoded.y),NearestCarrierHalf(oldEncoded.z));
    carrier.x=oldStored.x==.5?.5:carrier.x;
    carrier.y=oldStored.y==.5?.5:carrier.y;
    carrier.z=oldStored.z==.5?.5:carrier.z;
    float3 represented=(2*carrier.rgb-1)*range;
    tail=float4(delta-represented,any(represented!=0)?2:1);
}
[numthreads(8,8,1)]
void ExtractCS(uint3 id:SV_DispatchThreadID){
    if(any(id.xy>=WorkSize))return;
    float3 sum=0;float weight=0;bool valid=Valid();
    if(valid){
        // Exact rational area boundaries: avoid a spurious extra edge tap from
        // floating point ceil/floor on odd sizes. Numerators fit in uint.
        uint2 lo=id.xy*FullSize,hi=(id.xy+1)*FullSize;
        uint2 first=lo/WorkSize,last=(hi+WorkSize-1)/WorkSize;
        [loop]for(uint y=first.y;y<last.y;++y){
            uint wy=min(hi.y,(y+1)*WorkSize.y)-max(lo.y,y*WorkSize.y);
            [loop]for(uint x=first.x;x<last.x;++x){
                uint wx=min(hi.x,(x+1)*WorkSize.x)-max(lo.x,x*WorkSize.x);
                float w=float(wx)*float(wy);
                float3 b=Base.Load(int3(uint2(x,y)+BaseOrigin,0)).rgb;
                float3 z=Candidate.Load(int3(uint2(x,y)+CandidateOrigin,0)).rgb;
                bool finite=all(isfinite(b))&&all(isfinite(z));
                valid=valid&&finite;
                sum+=(finite?z-b:float3(0,0,0))*w;weight+=w;
            }
        }
    }
    float3 delta=sum/max(weight,1);
    valid=valid&&weight>0&&all(isfinite(delta));
    float4 carrier,tail;EncodePostResidualCarrier(delta,valid,CarrierRange,carrier,tail);
    CarrierOut[id.xy]=carrier;TailOut[id.xy]=tail;
}
[numthreads(8,8,1)]
void ComposeCS(uint3 id:SV_DispatchThreadID){
    if(any(id.xy>=FullSize)||!Valid()||!ReconstructionSucceeded)return;
    float4 b=Base.Load(int3(id.xy+BaseOrigin,0));
    // Target already contains this same pixel's Z. Preserve exact untouched
    // source pixels, including B alpha, before private SR can import bias.
    // Candidate is not bound as an SRV during Compose.
    float4 z=Target[id.xy+TargetOrigin];
    if(all(isfinite(b))&&all(z.rgb==b.rgb)){
        Target[id.xy+TargetOrigin]=b;
        return;
    }
    float3 carrier=Reconstructed.Load(int3(id.xy,0)).rgb;
    if(!all(isfinite(b))||!all(isfinite(carrier)))return;
    // Exact half-pixel phase. Float multiply/divide can turn a mathematical
    // zero-weight neighbour into a positive tap on NVIDIA (notably at 99%).
    // Such a tap must not import an invalid tail or a carrier-support tag.
    // Add one whole denominator before dividing to keep the arithmetic unsigned.
    uint2 numerator=(2*id.xy+1)*WorkSize+FullSize;
    uint2 denominator=2*FullSize;
    int2 origin=int2(numerator/denominator)-1;
    float2 f=float2(numerator%denominator)/float2(denominator);
    float3 tail=0;float total=0;bool hasCarrierEdit=false;
    bool3 channelEdit=bool3(false,false,false);
    [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){
        int2 q=clamp(origin+int2(x,y),int2(0,0),int2(WorkSize)-1);
        float4 value=Tail.Load(int3(q,0));float w=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        if(w>0&&(!all(isfinite(value))||value.a<.5))return;
        hasCarrierEdit=hasCarrierEdit||(w>0&&value.a>=1.5);
        if(w>0&&(Reserved&1)){
            // During Compose t0 is this exact recording's N-grid carrier,
            // never the candidate/target alias. FP16 neutral channels keep
            // their original tail-only contract independently of other RGB.
            float3 stored=Candidate.Load(int3(q,0)).rgb;
            if(!all(isfinite(stored)))return;
            channelEdit=channelEdit||((stored!=.5)&&(value.a>=1.5));
        }
        tail+=value.rgb*w;total+=w;
    }
    if(total<=0)return;
    float3 modelEdit=(hasCarrierEdit?(2*saturate(carrier)-1)*CarrierRange:float3(0,0,0));
    if(Reserved&1)modelEdit=float3(channelEdit.x?modelEdit.x:0,channelEdit.y?modelEdit.y:0,channelEdit.z?modelEdit.z:0);
    float3 delta=modelEdit+tail/total;
    // The target already contains Z. Invalid pixels keep that safe candidate.
    // Only B is the addition base; alpha is always B's alpha.
    float3 rgb=all(delta==0)?b.rgb:b.rgb+delta;
    if(all(isfinite(rgb))&&all(abs(rgb)<=65504))Target[id.xy+TargetOrigin]=float4(rgb,b.a);
}
