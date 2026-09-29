#include "nr_edit_scalar.hlsli"
// Normalize proven native active rectangles; no synthetic source identifiers.
cbuffer GuideConstants:register(b0) {
    uint2 OutputSize;uint2 DepthSize;uint2 MotionSize;uint2 DepthOrigin;
    uint2 MotionOrigin;float2 MotionScale;
    float ProjectionA;float ProjectionB;uint LinearDepth;uint Inverted;
    float2 RasterOffset;uint2 PhaseReserved;
};
Texture2D<float> DepthIn:register(t0);
Texture2D<float2> MotionIn:register(t1);
RWTexture2D<float> DepthOut:register(u0);
RWTexture2D<float2> MotionOut:register(u1);
[numthreads(8,8,1)]
void NormalizeGuidesCS(uint3 tid:SV_DispatchThreadID) {
    if(any(tid.xy>=OutputSize))return;
    float2 uv=(float2(tid.xy)+.5)/float2(OutputSize);
    // Jitter is the screen-space viewport displacement. Pull the sample at
    // the displaced raster location back onto the zero-jitter N grid. Select
    // depth and low-resolution MV together, never interpolate across surfaces.
    uint2 local=uint2(clamp(floor(uv*DepthSize+RasterOffset),0,float2(DepthSize-1)));
    uint2 d=local+DepthOrigin;
    uint2 m=(all(MotionSize==DepthSize)?local:min(uint2(uv*MotionSize),MotionSize-1))+MotionOrigin;
    float z=DepthIn.Load(int3(d,0));float2 mv=MotionIn.Load(int3(m,0))*MotionScale;
    if(LinearDepth)z=isfinite(z)&&z>1e-8?ProjectionA+ProjectionB/z:(Inverted?0:1);
    DepthOut[tid.xy]=isfinite(z)?saturate(z):(Inverted?0:1);
    // OutputSize is the normalized R grid. Invalid motion must not become
    // a stationary sample; send a finite out-of-bounds displacement instead.
    MotionOut[tid.xy]=all(isfinite(mv))?mv:2.0*float2(OutputSize);
}
