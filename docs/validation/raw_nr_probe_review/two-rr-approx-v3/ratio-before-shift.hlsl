// Independently authored experimental composition. No guide reconstruction.
// CopyResource(raw, composite) precedes this dispatch. Every refused/identity
// pixel receives no UAV write, preserving original FP16 bits and alpha.
Texture2D<float4> Raw : register(t0);
Texture2D<float4> Clean : register(t1);
Texture2D<float4> Edited : register(t2);
RWTexture2D<float4> Composite : register(u0);
cbuffer Args : register(b0) { uint Width; uint Height; float ShiftX; float ShiftY; };
[numthreads(8,8,1)]
void main(uint3 tid : SV_DispatchThreadID) {
 if(tid.x>=Width || tid.y>=Height) return;
 if(!all(isfinite(float2(ShiftX,ShiftY)))) return;
 float2 p=float2(tid.xy)+float2(ShiftX,ShiftY);
 int2 b=int2(floor(p)); float2 f=frac(p);
 float3 ratio=0; bool identity=true;
 [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
  float w=(x?f.x:1-f.x)*(y?f.y:1-f.y);
  if(w==0) continue; // zero-weight out-of-range/nonfinite taps are irrelevant
  int2 q=b+int2(x,y);
  if(any(q<0)||q.x>=int(Width)||q.y>=int(Height)) return;
  float3 c=Clean.Load(int3(q,0)).rgb,a=Edited.Load(int3(q,0)).rgb;
  if(!all(isfinite(c))||!all(isfinite(a))) return;
  bool same=all(c==a); identity=identity&&same;
  float3 r=1;
  if(!same) {
   if(any(c<=0)||any(a<0)) return;
   r=a/c;
   if(!all(isfinite(r))) return;
  }
  ratio+=w*r; // ratio first, then positive bilinear transport
 }
 if(identity) return;
 float4 raw=Raw.Load(int3(tid.xy,0));
 if(raw.a!=0.75) return; // actual frozen raw alpha 0x3a00; all unknown alpha bits fall back
 if(!all(isfinite(raw.rgb))||any(raw.rgb<0)) return;
 float3 value=raw.rgb*ratio;
 if(!all(isfinite(value))||any(value<0)||any(value>65504)) return;
 Composite[tid.xy]=float4(value,raw.a);
}
