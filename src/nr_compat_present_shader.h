#pragma once
// Own source, not extracted RenoDX DXIL. The Core still supplies its actual
// EncodeV2/CombineV2 kernels; these utilities only adapt the presentation surface.
inline constexpr char kCompatPresentShader[] = R"HLSL(
Texture2D<float4> Raw : register(t0);
Texture2D<float4> Canonical : register(t1);
Texture2D<float4> Composed : register(t2);
RWTexture2D<float4> Destination : register(u0);
RWTexture2D<float> DummyDepth : register(u1);
RWTexture2D<float2> DummyMotion : register(u2);
cbuffer Constants : register(b0) { uint Width; uint Height; uint Encoding; uint Padding; };
float3 ToLinear(float3 x) { return float3(x.r<=0.04045?x.r/12.92:pow((x.r+0.055)/1.055,2.4),x.g<=0.04045?x.g/12.92:pow((x.g+0.055)/1.055,2.4),x.b<=0.04045?x.b/12.92:pow((x.b+0.055)/1.055,2.4)); }
float3 ToSrgb(float3 x) {return float3(x.r<=0.0031308?12.92*x.r:1.055*pow(max(x.r,0),1/2.4)-0.055,x.g<=0.0031308?12.92*x.g:1.055*pow(max(x.g,0),1/2.4)-0.055,x.b<=0.0031308?12.92*x.b:1.055*pow(max(x.b,0),1/2.4)-0.055);}
float3 FromPq(float3 x) {
    float3 a=pow(saturate(x),32.0/2523.0);
    return pow(max(a-3424.0/4096.0,0)/max(2413.0/128.0-(2392.0/128.0)*a,1e-12),16384.0/2610.0);
}
float3 ToPq(float3 x) {
    float3 a=pow(saturate(x),2610.0/16384.0);
    return pow((3424.0/4096.0+(2413.0/128.0)*a)/(1+(2392.0/128.0)*a),2523.0/32.0);
}
float3 From2020(float3 c) {
    return mul(float3x3(1.660491,-0.587641,-0.072850,-0.124550,1.132900,-0.008350,-0.018151,-0.100579,1.118730),c);
}
float3 To2020(float3 c) {
    return mul(float3x3(0.627404,0.329283,0.043313,0.069097,0.919540,0.011362,0.016391,0.088013,0.895595),c);
}
bool Finite(float3 v) {return all(isfinite(v))&&all(abs(v)<=65504.0);}
[numthreads(8,8,1)] void ConvertCS(uint3 p:SV_DispatchThreadID) {
    if(p.x>=Width||p.y>=Height)return;
    float4 raw=Raw.Load(int3(p.xy,0));float3 c=raw.rgb;
    if(Encoding==1)c=ToLinear(saturate(c));
    if(Encoding==3)c=From2020(FromPq(c)*125.0); // scRGB: 1=80 nits; PQ:1=10000 nits
    Destination[p.xy]=float4(Finite(c)?c:0.0.xxx,raw.a);
}
[numthreads(8,8,1)] void InitCS(uint3 p:SV_DispatchThreadID) {
    if(p.x>=Width||p.y>=Height)return;
    DummyDepth[p.xy]=1.0;DummyMotion[p.xy]=0.0.xx;
}
[numthreads(8,8,1)] void InitColorCS(uint3 p:SV_DispatchThreadID) { if(p.x<Width&&p.y<Height) Destination[p.xy]=float4(0,0,0,1); }
struct Varying {float4 position:SV_Position;};
Varying FullscreenVS(uint index:SV_VertexID) {
    Varying o;float2 p=float2((index<<1)&2,index&2);o.position=float4(p*float2(2,-2)+float2(-1,1),0,1);return o;
}
float4 BlitPS(Varying p):SV_Target {
    int2 xy=int2(p.position.xy);float4 raw=Raw.Load(int3(xy,0));
    float3 base=Canonical.Load(int3(xy,0)).rgb,edited=Composed.Load(int3(xy,0)).rgb;
    // Preserve exact original encoded values on no-op or invalid results,
    // including fp16 conversion/roundtrip effects and original source alpha.
    if(!Finite(edited)||all(edited==base))return raw;
    float3 c=edited;
    if(Encoding==1)c=saturate(ToSrgb(c));
    if(Encoding==3)c=ToPq(max(To2020(c),0)/125.0);
    return all(isfinite(c))?float4(c,raw.a):raw;
}
)HLSL";
