// Project-owned conversion from PR256 blob; no vendor algorithm is implemented here.
#pragma once
namespace nr::final_residual {
// Original project code. PREP uses exact area-overlap weights (low-pass), not
// skipped texels. At 100% each destination is exactly one Source.Load.
inline constexpr char amd_conversion_shader[] = R"hlsl(
cbuffer Params : register(b0) {
    uint SourceWidth; uint SourceHeight; uint FlowWidth; uint FlowHeight;
    uint Grid; uint Reset; float UnitX; float UnitY;
    uint LinearHdr; float SourceToScene; uint Padding0; uint Padding1;
};
#ifdef PREP
Texture2D<float4> Source : register(t0);
#ifdef PREP_RGBA
RWTexture2D<float4> Target : register(u0);
#else
RWTexture2D<float> Target : register(u0);
#endif
float4 Filter(uint2 p) {
    if (SourceWidth == FlowWidth && SourceHeight == FlowHeight)
        return Source.Load(int3(p,0));
    float2 lo = float2(p) * float2(SourceWidth,SourceHeight) / float2(FlowWidth,FlowHeight);
    float2 hi = float2(p+1) * float2(SourceWidth,SourceHeight) / float2(FlowWidth,FlowHeight);
    uint2 first = uint2(floor(lo));
    uint2 end = min(uint2(ceil(hi)),uint2(SourceWidth,SourceHeight));
    float4 sum = 0; float weight = 0;
    for (uint y=first.y; y<end.y; ++y) {
        for (uint x=first.x; x<end.x; ++x) {
            float2 overlap = max(0.0, min(hi,float2(x+1,y+1))-max(lo,float2(x,y)));
            float w = overlap.x * overlap.y;
            sum += Source.Load(int3(x,y,0)) * w; weight += w;
        }
    }
    return sum / max(weight,1e-8);
}
[numthreads(8,8,1)] void CS(uint3 p : SV_DispatchThreadID) {
    if (p.x >= FlowWidth || p.y >= FlowHeight) return;
    float4 c=Filter(p.xy);
    if(LinearHdr!=0){
        // Only the unenhanced flow guide is compressed. Residuals/output remain
        // scene-linear, so bright HDR values are never clipped to SDR white.
        float3 scene=c.rgb*SourceToScene;
        scene=all(isfinite(scene))?scene:0;
        c.rgb=0.5+0.5*scene/(1+abs(scene));
    }
#ifdef PREP_RGBA
    Target[p.xy]=c;
#else
    Target[p.xy]=dot(saturate(c.rgb),float3(0.2126,0.7152,0.0722));
#endif
}
#else
Texture2D<int2> Raw : register(t0);
RWTexture2D<float2> Flow : register(u0);
[numthreads(8,8,1)] void CS(uint3 p : SV_DispatchThreadID) {
    if (p.x >= SourceWidth || p.y >= SourceHeight) return;
    // Native block expansion, with the actual source-to-flow coordinate mapping.
    uint2 q=p.xy;
    if (SourceWidth!=FlowWidth || SourceHeight!=FlowHeight)
        q=min(uint2((float2(p.xy)+0.5)*float2(FlowWidth,FlowHeight)/float2(SourceWidth,SourceHeight)),
              uint2(FlowWidth-1,FlowHeight-1));
    float2 v=0;
    if (Reset==0) v=float2(Raw.Load(int3(q/Grid,0)))*float2(UnitX,UnitY);
    Flow[p.xy]=v;
}
#endif
)hlsl";
} // namespace nr::final_residual
