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
RWTexture2D<float4> OutGuide : register(u0);
RWTexture2D<float4> OutField : register(u1);
cbuffer C : register(b0) {
 uint W,H,MW,MH;
 uint RW,RH,Flags,Encoded;
 uint DX,DY,NX,NY;
 uint SX,SY,HX,HY;
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
float3 Pair(int2 p) {
 p=clamp(p,0,int2(W-1,H-1));uint2 m=min(uint2((float2(p)+.5)*float2(MW,MH)/float2(W,H)),uint2(MW-1,MH-1));
 float3 a=Proxy.Load(int3(m,0)).rgb,b=Neural.Load(int3(m,0)).rgb;
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
