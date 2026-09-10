#ifndef NR_DUAL_RESOLVE_CACHED_HLSLI
#define NR_DUAL_RESOLVE_CACHED_HLSLI
#include "nr_dual_math.hlsli"
// LowDelta stores the exact FP32 matched difference once on N2. The original
// L1/L2 remain intact for NGX history and capture. This accessor still fuses
// restoration into the existing final read; it adds no N1 write or new SR.
float4 NRDualFinalStored(int2 pixel,uint2 mainSize,uint2 lowSize,uint domain) {
    pixel=clamp(pixel,0,int2(mainSize)-1);
    float4 primary=NR_DUAL_MAIN.Load(int3(pixel,0));
    if(!NRDualFinite(primary.rgb)||any(lowSize==0)||domain>1)return primary;
    float2 pos=(float2(pixel)+.5)*float2(lowSize)/float2(mainSize)-.5;
    int2 base=int2(floor(pos));float2 f=frac(pos);
    float3 delta=0;bool valid=true;
    [unroll] for(uint i=0;i<4;i++) {
        int2 o=int2(i&1u,i>>1);float weight=(o.x?f.x:1-f.x)*(o.y?f.y:1-f.y);
        if(weight<=0)continue;
        float4 tap=NR_DUAL_DELTA.Load(int3(clamp(base+o,0,int2(lowSize)-1),0));
        if(tap.a!=1||!all(isfinite(tap.rgb))){valid=false;continue;}
        delta+=weight*tap.rgb;
    }
    if(!valid||all(delta==0))return primary;
    float3 result=NRDualEncode(NRDualDecode(primary.rgb,domain)+delta,domain);
    return NRDualFinite(result)?float4(result,primary.a):primary;
}
#endif
