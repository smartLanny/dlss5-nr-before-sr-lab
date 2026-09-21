#ifndef NR_EDIT_SCALAR_HLSLI
#define NR_EDIT_SCALAR_HLSLI
// Inspect IEEE exponent bits explicitly; FXC can otherwise fold finite checks
// even on code compiled with /Gis. Preserve NaN/Inf rejection under optimization.
bool NrEditFinite(float v){return (asuint(v)&0x7f800000u)!=0x7f800000u;}
bool2 NrEditFinite(float2 v){return (asuint(v)&0x7f800000u)!=0x7f800000u;}
bool3 NrEditFinite(float3 v){return (asuint(v)&0x7f800000u)!=0x7f800000u;}
bool4 NrEditFinite(float4 v){return (asuint(v)&0x7f800000u)!=0x7f800000u;}
#define isfinite NrEditFinite
float NrDepthWeight(float expected,float previous,float confidence,float absoluteLimit,float relativeLimit){
    if(!isfinite(expected)||!isfinite(previous)||!isfinite(confidence)||expected<=0||previous<=0||
       !isfinite(absoluteLimit)||!isfinite(relativeLimit)||absoluteLimit<0||relativeLimit<0)return 0;
    float tolerance=max(absoluteLimit,relativeLimit*max(expected,previous));
    return isfinite(tolerance)&&abs(expected-previous)<=tolerance?saturate(confidence):0;
}
float NrViewZ(float depth,float a,float b){
    float d=depth-a;if(!isfinite(depth)||!isfinite(a)||!isfinite(b)||depth<0||depth>1||abs(d)<1e-8)return 0;
    float z=b/d;return isfinite(z)&&z>0?z:0;
}
float3 NrAccumulate(float3 current,float3 previous,float weight,float rate,float3 lo,float3 hi){
    if(!all(isfinite(current)))return 0;
    if(!all(isfinite(previous))||!isfinite(weight)||!isfinite(rate))return current;
    float a=max(clamp(rate,.01,1),1-saturate(weight));
    float3 v=lerp(clamp(previous,lo,hi),current,a);return all(isfinite(v))?v:current;
}
#endif
