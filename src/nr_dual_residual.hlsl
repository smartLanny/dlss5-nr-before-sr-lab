#include "nr_dual_math.hlsli"
Texture2D<float4> LowInput : register(t0);
Texture2D<float4> LowOutput : register(t1);
Texture2D<uint> LowValid : register(t2);
RWTexture2D<float4> LowDelta : register(u0);
cbuffer C : register(b0) {uint W,H,Domain,Reserved;};
[numthreads(8,8,1)] void ResidualCS(uint3 p:SV_DispatchThreadID) {
    if(p.x>=W||p.y>=H)return;
    float3 a=LowInput.Load(int3(p.xy,0)).rgb,b=LowOutput.Load(int3(p.xy,0)).rgb;
    bool valid=LowValid.Load(int3(p.xy,0))!=0u&&NRDualFinite(a)&&NRDualFinite(b)&&Domain<=1u;
    float3 delta=0;
    if(valid){delta=NRDualDecode(b,Domain)-NRDualDecode(a,Domain);valid=all(isfinite(delta));}
    LowDelta[p.xy]=valid?float4(delta,1):0;
}
