#pragma once
#include <string>
namespace store_interval_fixture {
// The fixture controls only synthetic producer values/faults. Bounds, record
// writes, probe reads and aggregation remain the actual production functions.
inline std::string Shader(unsigned mode) {
    return std::string(kCodecShader)+"\n#define NR_INTERVAL_FIXTURE_MODE "+std::to_string(mode)+R"HLSL(
[numthreads(8,8,1)] void StoreIntervalFixtureCS(uint3 tid:SV_DispatchThreadID) {
    uint2 coord=tid.xy;if(coord.x>=Width || coord.y>=Height)return;
    float3 expected=OriginalColor.Load(int3(coord+uint2(SourceBaseX,SourceBaseY),0)).rgb;
    float3 actual=expected;
    uint maximum=((Passthrough>>20u)&7u)==2u?255u:1023u;
#if NR_INTERVAL_FIXTURE_MODE >= 1 && NR_INTERVAL_FIXTURE_MODE <= 3
    uint2 r=StoreUnormBounds(expected.r,maximum),g=StoreUnormBounds(expected.g,maximum),b=StoreUnormBounds(expected.b,maximum);
#if NR_INTERVAL_FIXTURE_MODE == 1
    actual=float3(r.x,g.x,b.x)/(float)maximum;
#elif NR_INTERVAL_FIXTURE_MODE == 2
    actual=float3(r.y,g.y,b.y)/(float)maximum;
#else
    actual.r=(float)(r.x>0u?r.x-1u:r.y+1u)/(float)maximum;
#endif
#elif NR_INTERVAL_FIXTURE_MODE == 6
    expected.r=asfloat(0x7fc12345u);actual.r=0.0;
#endif
#if NR_INTERVAL_FIXTURE_MODE != 5
    ResultOut[coord+uint2(DestinationBaseX,DestinationBaseY)]=float4(actual,1.0);
#endif
#if NR_INTERVAL_FIXTURE_MODE == 4
    if(coord.x==32u && coord.y==0u)return;
#endif
    RecordPostStoreExpected(coord,expected);
#if NR_INTERVAL_FIXTURE_MODE == 7
    if(coord.x==32u && coord.y==0u)RecordPostStoreExpected(coord,expected);
#elif NR_INTERVAL_FIXTURE_MODE == 8
    if(coord.x==32u && coord.y==0u) {
        uint address;if(StoreIntervalAddress(coord,address))DeltaStats.Store(address+4u,0u);
    }
#elif NR_INTERVAL_FIXTURE_MODE == 9
    if(coord.x==0u && coord.y==0u)DeltaStats.Store(160u,0u);
#endif
}
)HLSL";
}
}
