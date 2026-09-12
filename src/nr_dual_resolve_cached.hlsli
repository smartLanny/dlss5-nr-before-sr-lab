#ifndef NR_DUAL_RESOLVE_CACHED_HLSLI
#define NR_DUAL_RESOLVE_CACHED_HLSLI
#include "nr_dual_math.hlsli"
// LowDelta stores the exact FP32 matched difference once on N2. The original
// L1/L2 remain intact for NGX history and capture. This accessor still fuses
// restoration into the existing final read; it adds no N1 write or new SR.
float4 NRDualFinalStored(int2 pixel,uint2 mainSize,uint2 lowSize,uint domain) {
    const bool legal=all(mainSize!=0)&&all(lowSize!=0)&&domain<=1;
    mainSize=max(mainSize,1u);lowSize=max(lowSize,1u);
    pixel=clamp(pixel,0,int2(mainSize)-1);
    float4 primary=NR_DUAL_MAIN.Load(int3(pixel,0));
    float2 pos=(float2(pixel)+.5)*float2(lowSize)/float2(mainSize)-.5;
    int2 base=int2(floor(pos));float2 f=frac(pos);
    // Explicit four taps keep SM5's inliner from expanding an unrolled loop at
    // every existing neighbourhood read. Invalid values are selected away
    // before multiplication, so zero-weight invalid taps remain harmless.
    int2 upper=int2(lowSize)-1;
    float4 a=NR_DUAL_DELTA.Load(int3(clamp(base,0,upper),0));
    float4 b=NR_DUAL_DELTA.Load(int3(clamp(base+int2(1,0),0,upper),0));
    float4 c=NR_DUAL_DELTA.Load(int3(clamp(base+int2(0,1),0,upper),0));
    float4 d=NR_DUAL_DELTA.Load(int3(clamp(base+int2(1,1),0,upper),0));
    bool av=a.a==1&&all(isfinite(a.rgb)),bv=b.a==1&&all(isfinite(b.rgb));
    bool cv=c.a==1&&all(isfinite(c.rgb)),dv=d.a==1&&all(isfinite(d.rgb));
    float wa=(1-f.x)*(1-f.y),wb=f.x*(1-f.y),wc=(1-f.x)*f.y,wd=f.x*f.y;
    float3 delta=wa*(av?a.rgb:0)+wb*(bv?b.rgb:0)+wc*(cv?c.rgb:0)+wd*(dv?d.rgb:0);
    bool valid=legal&&NRDualFinite(primary.rgb)&&(wa<=0||av)&&(wb<=0||bv)&&(wc<=0||cv)&&(wd<=0||dv);
    float3 result=NRDualEncode(NRDualDecode(primary.rgb,domain)+delta,domain);
    const bool apply=valid&&any(delta!=0)&&NRDualFinite(result);
    return float4(apply?result:primary.rgb,primary.a);
}
#endif
