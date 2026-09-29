// Beta12 opt-in same-frame signed-delta experiments. OriginalColor is B,
// ProxyColor is Z, DualLowInput/Output are FP32 area B/delta. No history.
float SpatialPeak(float3 v) { return max(max(abs(v.x),abs(v.y)),abs(v.z)); }
float SpatialGuide(float3 a,float3 b) {
    float scale=max(max(SpatialPeak(a),SpatialPeak(b)),1.0f/4096.0f);
    float d=SpatialPeak(a/scale-b/scale);
    return 1.0f/(1.0f+64.0f*d*d);
}
bool SpatialFinite(float3 v) { return all(isfinite(v)); }
float4 SpatialB(uint2 p) { return OriginalColor.Load(int3(p,0)); }
float4 SpatialZ(uint2 p) { return ProxyColor.Load(int3(p+uint2(SourceBaseX,SourceBaseY),0)); }

// Exact integer-overlap area average, as in the Beta8 FP32 reference.
[numthreads(8,8,1)]
void SpatialAreaCS(uint3 tid:SV_DispatchThreadID) {
    if(tid.x>=DualLowSize.x||tid.y>=DualLowSize.y)return;
    uint2 lo=tid.xy*uint2(Width,Height);
    uint2 hi=(tid.xy+1)*uint2(Width,Height);
    uint2 first=lo/DualLowSize;
    uint2 last=(hi+DualLowSize-1)/DualLowSize;
    float3 bsum=0,dsum=0;float total=0;bool valid=true;
    for(uint y=first.y;y<last.y;++y)for(uint x=first.x;x<last.x;++x){
        uint wx=min(hi.x,(x+1)*DualLowSize.x)-max(lo.x,x*DualLowSize.x);
        uint wy=min(hi.y,(y+1)*DualLowSize.y)-max(lo.y,y*DualLowSize.y);
        float w=float(wx)*float(wy);
        float3 b=SpatialB(uint2(x,y)).rgb,z=SpatialZ(uint2(x,y)).rgb;
        valid=valid&&SpatialFinite(b)&&SpatialFinite(z);
        bsum+=b*w;dsum+=(z-b)*w;total+=w;
    }
    if(!valid||!isfinite(total)||total<=0||!SpatialFinite(bsum)||!SpatialFinite(dsum)){
        ColorEditFieldOut[tid.xy]=float4(0,0,0,-1);
        LanczosIntermediateOut[tid.xy]=float4(0,0,0,-1);
        return;
    }
    ColorEditFieldOut[tid.xy]=float4(bsum/total,1);
    LanczosIntermediateOut[tid.xy]=float4(dsum/total,1);
}

// Four positive spatial taps with the original B-guided Beta8 weights.
[numthreads(8,8,1)]
void SpatialGuidedCS(uint3 tid:SV_DispatchThreadID){
    if(tid.x>=Width||tid.y>=Height)return;
    float4 b=SpatialB(tid.xy),z=SpatialZ(tid.xy);
    float4 answer=z;
    if(SpatialFinite(b.rgb)&&SpatialFinite(z.rgb)&&all(b.rgb==z.rgb))answer=b;
    else if(SpatialFinite(b.rgb)&&SpatialFinite(z.rgb)){
        // Exact Beta8 half-pixel integer phase, including 99% and odd extents.
        uint2 numerator=(2u*tid.xy+1u)*DualLowSize+uint2(Width,Height);
        uint2 denominator=2u*uint2(Width,Height);
        int2 lo=int2(numerator/denominator)-1;
        float2 fraction=float2(numerator%denominator)/float2(denominator);
        float3 sum=0;float weight_sum=0;bool valid=true;
        [unroll]for(uint j=0;j<2;++j)[unroll]for(uint i=0;i<2;++i){
            float spatial=(i?fraction.x:1-fraction.x)*(j?fraction.y:1-fraction.y);
            if(spatial==0)continue;
            int2 q=clamp(int2(lo)+int2(i,j),int2(0,0),int2(DualLowSize)-1);
            float4 lb=DualLowInput.Load(int3(q,0));
            float4 ld=DualLowOutput.Load(int3(q,0));
            valid=valid&&lb.a>0&&ld.a>0&&SpatialFinite(lb.rgb)&&SpatialFinite(ld.rgb);
            float w=spatial*SpatialGuide(b.rgb,lb.rgb);
            sum+=ld.rgb*w;weight_sum+=w;
        }
        if(valid&&isfinite(weight_sum)&&weight_sum>0){
            float3 rgb=b.rgb+sum/weight_sum;
            if(SpatialFinite(rgb)&&SpatialPeak(rgb)<=65504.0f)answer=float4(rgb,b.a);
        }
    }
    ResultOut[tid.xy]=answer;
}

// Distinct mode B: full-resolution 3x3 B-guided local mean of signed delta.
// Flat B gives a mean that suppresses isolated NR speckles; constant positive
// or negative edits remain constant. It never reads previous frames.
[numthreads(8,8,1)]
void SpatialLocalCS(uint3 tid:SV_DispatchThreadID){
    if(tid.x>=Width||tid.y>=Height)return;
    float4 b=SpatialB(tid.xy),z=SpatialZ(tid.xy);
    float4 answer=z;
    if(SpatialFinite(b.rgb)&&SpatialFinite(z.rgb)&&all(b.rgb==z.rgb))answer=b;
    else if(SpatialFinite(b.rgb)&&SpatialFinite(z.rgb)){
        float3 sum=0;float weight_sum=0;bool valid=true;
        [unroll]for(int j=-1;j<=1;++j)[unroll]for(int i=-1;i<=1;++i){
            uint2 q=uint2(clamp(int(tid.x)+i,0,int(Width)-1),clamp(int(tid.y)+j,0,int(Height)-1));
            float3 qb=SpatialB(q).rgb,qz=SpatialZ(q).rgb;
            valid=valid&&SpatialFinite(qb)&&SpatialFinite(qz);
            float w=SpatialGuide(b.rgb,qb)*(i==0&&j==0?1.0f:0.75f);
            sum+=(qz-qb)*w;weight_sum+=w;
        }
        if(valid&&isfinite(weight_sum)&&weight_sum>0&&SpatialFinite(sum)){
            float3 rgb=b.rgb+sum/weight_sum;
            if(SpatialFinite(rgb)&&SpatialPeak(rgb)<=65504.0f)answer=float4(rgb,b.a);
        }
    }
    ResultOut[tid.xy]=answer;
}
