// Area-filter resolved SR/RR colour before the existing Mode1 proxy mapping.
// SourceWidth/Height describe O; Width/Height describe N. At N==O the original
// EncodeCS load stays unchanged and never calls this helper.
float4 LoadPostArea(uint2 pixel) {
    float2 lo=float2(pixel)*float2(SourceWidth,SourceHeight)/float2(Width,Height);
    float2 hi=float2(pixel+1u)*float2(SourceWidth,SourceHeight)/float2(Width,Height);
    int2 first=int2(floor(lo)), last=int2(ceil(hi));
    float4 total=0.0.xxxx;
    float weight=0.0;
    const bool encoded=(Passthrough&32u)!=0u;
    const bool pq=(Passthrough&128u)!=0u;
    [loop] for(int y=first.y;y<last.y;++y) {
        float wy=max(0.0,min(hi.y,float(y+1))-max(lo.y,float(y)));
        [loop] for(int x=first.x;x<last.x;++x) {
            float wx=max(0.0,min(hi.x,float(x+1))-max(lo.x,float(x)));
            float w=wx*wy;
            float4 value=LoadSourceStoredV2(int2(x,y));
            if(encoded) value.rgb=SRGBToLinear(value.rgb);
            else if(pq) value.rgb=PQToLinear(value.rgb);
            total+=value*w; weight+=w;
        }
    }
    float4 filtered=total/max(weight,0.000001);
    if(encoded) filtered.rgb=LinearToSRGB(filtered.rgb);
    else if(pq) filtered.rgb=LinearToPQ(filtered.rgb);
    // Only the opaque model input changes alpha. Combine restores native alpha.
    filtered.a=1.0;
    return filtered;
}
