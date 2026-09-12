#pragma once
// Fixed pre-gain selection. Engine inputs are snapshotted before native SR/RR.
inline constexpr char kRegionShader[] = R"HLSL(
Texture2D<float4> Depth : register(t0);
Texture2D<float4> Normals : register(t1);
Texture2D<float4> Specular : register(t2);
Texture2D<float4> Hit : register(t3);
Texture2D<float4> Proxy : register(t4);
Texture2D<float4> Neural : register(t5);
Texture2D<float4> Guide : register(t6);
Texture2D<float4> DualDelta : register(t7);
RWTexture2D<float4> OutGuide : register(u0);
RWTexture2D<float4> OutField : register(u1);
cbuffer C : register(b0) {
 uint W,H,MW,MH;
 uint RW,RH,Flags,Encoded;
 uint DX,DY,NX,NY;
 uint SX,SY,HX,HY;
 uint DualW,DualH,DualActive,DualDomain;
};
static const float3 L=float3(.2126,.7152,.0722);
float3 Linear(float3 c) { c=saturate(c);return lerp(c/12.92,pow((c+.055)/1.055,2.4),step(.04045.xxx,c)); }
float2 Oct(float3 n) { n/=dot(abs(n),1.0.xxx);return n.z>=0?n.xy:(1-abs(n.yx))*float2(n.x>=0?1:-1,n.y>=0?1:-1); }
float3 UnOct(float2 p) {float3 n=float3(p,1-abs(p.x)-abs(p.y));float t=saturate(-n.z);n.xy+=float2(n.x>=0?-t:t,n.y>=0?-t:t);return normalize(n);}
[numthreads(8,8,1)] void SnapshotCS(uint3 id:SV_DispatchThreadID) {
 if(id.x>=W||id.y>=H)return;
 uint2 p=min(uint2((float2(id.xy)+.5)*float2(RW,RH)/float2(W,H)),uint2(RW-1,RH-1));
 float d=-1;float2 n=0;float nw=0,r=0;
 if(Flags&1u){float v=Depth.Load(int3(p+uint2(DX,DY),0)).r;if(isfinite(v)&&v>=0)d=v;}
 if(Flags&2u){float3 v=Normals.Load(int3(p+uint2(NX,NY),0)).rgb;float q=dot(v,v);if(all(isfinite(v))&&q>.5&&q<1.5){n=Oct(normalize(v));nw=2;}}
 if((Flags&12u)==12u){float3 s=Specular.Load(int3(p+uint2(SX,SY),0)).rgb;float h=Hit.Load(int3(p+uint2(HX,HY),0)).r;
  if(all(isfinite(s))&&all(s>=0)&&isfinite(h)&&h>0)r=smoothstep(.01,.12,max(s.r,max(s.g,s.b)));}
 OutGuide[id.xy]=float4(d,n,nw+r);
}
// DUAL_REGION_EMBEDDED_BEGIN
#define NR_DUAL_MAIN Neural
#define NR_DUAL_DELTA DualDelta
#ifndef NR_DUAL_RESOLVE_CACHED_HLSLI
#define NR_DUAL_RESOLVE_CACHED_HLSLI
#ifndef NR_DUAL_MATH_HLSLI
#define NR_DUAL_MATH_HLSLI
// Domain 0: existing direct/model-value path. Domain 1: existing sRGB Proxy path.
// Never call these functions on raw PQ, scRGB scene light or a signed delta.
bool NRDualFinite(float3 v) {
    return all(isfinite(v)) && all(abs(v) <= 65504.0.xxx);
}
float NRDualDecodeChannel(float x) {
    return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}
float NRDualEncodeChannel(float x) {
    // Scalar branch avoids a fractional power of a negative residual-containing value.
    return x <= 0.0031308 ? x * 12.92 : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}
float3 NRDualDecode(float3 v, uint domain) {
    if (domain == 0u) return v;
    return float3(NRDualDecodeChannel(v.r), NRDualDecodeChannel(v.g), NRDualDecodeChannel(v.b));
}
float3 NRDualEncode(float3 v, uint domain) {
    if (domain == 0u) return v;
    return float3(NRDualEncodeChannel(v.r), NRDualEncodeChannel(v.g), NRDualEncodeChannel(v.b));
}
#endif

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

// DUAL_REGION_EMBEDDED_END
float3 Pair(int2 p) {
 p=clamp(p,0,int2(W-1,H-1));uint2 m=min(uint2((float2(p)+.5)*float2(MW,MH)/float2(W,H)),uint2(MW-1,MH-1));
 float3 a=Proxy.Load(int3(m,0)).rgb,b=Neural.Load(int3(m,0)).rgb;
 [branch] if(DualActive!=0u)b=NRDualFinalStored(int2(m),uint2(MW,MH),uint2(DualW,DualH),DualDomain).rgb;
 if(!all(isfinite(a))||!all(isfinite(b))||any(abs(a)>65504)||any(abs(b)>65504))return float3(0,-1,0);
 if(!Encoded){a=Linear(a);b=Linear(b);}float y=max(dot(a,L),0),z=max(dot(b,L),0);
 return float3(clamp(log2(max(z,1.0/4096)/max(y,1.0/4096)),-8,8),y,1);
}
float SameSurface(float4 a,float4 b,float ay,float by){
 float w=1-smoothstep(.10,.50,abs(ay-by)/max(max(ay,by),1.0/1024));
 if(a.x>=0){if(b.x<0)return 0;float tolerance=(Flags&16u)?max(abs(a.x)*.015,.001):.0002;
 w*=1-smoothstep(tolerance,tolerance*4,abs(a.x-b.x));}
 if(a.w>=2 && b.w>=2)w*=smoothstep(.7,.95,dot(UnOct(a.yz),UnOct(b.yz)));
 return w;
}
[numthreads(8,8,1)] void BuildCS(uint3 id:SV_DispatchThreadID){
 if(id.x>=W||id.y>=H)return;int2 p=int2(id.xy);float3 c=Pair(p);float4 g=Guide.Load(int3(p,0));
 if(c.z!=1){OutField[p]=float4(0,0,-1,-1);return;}
 // The broad estimate excludes the centre: an isolated edit remains detail.
 const int2 offsets[12]={int2(2,0),int2(-2,0),int2(0,2),int2(0,-2),int2(6,6),int2(-6,6),int2(6,-6),int2(-6,-6),int2(16,0),int2(-16,0),int2(0,16),int2(0,-16)};
 float sum=0,weight=0;
 [unroll]for(uint i=0;i<12;i++){int2 q=clamp(p+offsets[i],0,int2(W-1,H-1));float3 t=Pair(q);if(t.z!=1)continue;
  float w=SameSurface(g,Guide.Load(int3(q,0)),c.y,t.y);sum+=max(sign(c.x)*t.x,0)*w;weight+=w;}
 float broad=weight>0?saturate(sum/(weight*max(abs(c.x),.0001))):0;
 float selected=abs(c.x)>.0001?sign(c.x)*(1+broad):0;
 OutField[p]=float4(selected,g.w>=2?g.w-2:g.w,g.x,c.y);
}
)HLSL";
