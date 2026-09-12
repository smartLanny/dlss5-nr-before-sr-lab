#pragma once
const char kRrDepthShader[] = R"HLSL(
Texture2D<float> ViewDepth : register(t0);
RWTexture2D<float> HardwareDepth : register(u0);
cbuffer DepthConstants : register(b0) {
    uint Width, Height, BaseX, BaseY;
    float ProjectionA, ProjectionB, FarDepth, Padding;
};
[numthreads(8,8,1)]
void ConvertRrDepthCS(uint3 tid : SV_DispatchThreadID) {
    if (tid.x>=Width || tid.y>=Height) return;
    float z=ViewDepth.Load(int3(tid.xy+uint2(BaseX,BaseY),0));
    // RR view-space background/invalid samples have no finite surface. Map
    // them to the explicit far plane, never feed NaN/Inf to Feature18.
    float d=FarDepth;
    if (isfinite(z) && z>0) {
        float projected=ProjectionA+ProjectionB/z;
        if (isfinite(projected)) d=saturate(projected);
    }
    HardwareDepth[tid.xy]=d;
}
)HLSL";
