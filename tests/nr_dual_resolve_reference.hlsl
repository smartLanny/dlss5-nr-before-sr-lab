// Numerical/reference GPU fixture. Production should include the same function
// in its existing final codec instead of writing this extra full-size texture.
Texture2D<float4> MainAnswer : register(t0);
Texture2D<float4> LowInput : register(t1);
Texture2D<float4> LowAnswer : register(t2);
Texture2D<uint> LowValid : register(t3);
RWTexture2D<float4> Result : register(u0);
cbuffer DualResolveConstants : register(b0) {
    uint2 MainSize;
    uint2 LowSize;
    uint ModelDomain;
    uint3 Reserved;
};
#define NR_DUAL_MAIN MainAnswer
#define NR_DUAL_LOW_IN LowInput
#define NR_DUAL_LOW_OUT LowAnswer
#define NR_DUAL_VALID LowValid
#include "nr_dual_resolve.hlsli"
[numthreads(8, 8, 1)]
void ResolveReferenceCS(uint3 tid : SV_DispatchThreadID) {
    if (any(tid.xy >= MainSize)) return;
    Result[tid.xy] = NRDualFinalStored(int2(tid.xy), MainSize, LowSize, ModelDomain);
}
