// Independent source-guided reconstruction of SAME weights applied to P and N.
// The caller admits only mapped, reduced, same-call planes. No new resource.
#ifndef NR_RECONSTRUCTION_SAMPLING_HLSLI
#define NR_RECONSTRUCTION_SAMPLING_HLSLI
uint NrReconMode() { return (RegionFlags >> 8u) & 3u; }
bool NrReconScaled() {
    uint mode=NrReconMode();
    return (mode==1u||mode==2u)&&(Width!=ModelWidth||Height!=ModelHeight);
}
void NrReconSamplePair(float2 center,float3 reference,
    out float3 proxy,out float3 neural,out bool valid) {
    float3 sumP=0.0.xxx,sumN=0.0.xxx;
    float sumW=0.0;
    uint invalid=(!all(isfinite(reference))||any(reference<0.0))?1u:0u;
    float2 mapped=center*float2(ModelWidth,ModelHeight)/
        float2(max(Width,1u),max(Height,1u))-0.5;
    int2 base=int2(floor(mapped));float2 phase=frac(mapped);
    uint mode=NrReconMode();uint count=mode==2u?16u:4u;
    // Scalar loop-carried state, no returned struct or early loop exits.
    [loop] for(uint i=0u;i<count;++i) {
        int2 offset=mode==2u?int2(int(i&3u)-1,int(i>>2u)-1):int2(i&1u,i>>1u);
        float spatial=mode==2u
            ?NrReconCubicWeight(float(offset.x)-phase.x)*NrReconCubicWeight(float(offset.y)-phase.y)
            :(offset.x!=0?phase.x:1.0-phase.x)*(offset.y!=0?phase.y:1.0-phase.y);
        [branch] if(spatial>0.0) {
            int2 p=clamp(base+offset,int2(0,0),int2(max(ModelWidth,1u),max(ModelHeight,1u))-1);
            float3 storedP=ProxyColor.Load(int3(p,0)).rgb;
            float3 storedN=LoadFinalNeuralStoredV2(p).rgb;
            float3 sampleP=(Passthrough&1u)!=0u?storedP:SRGBToLinear(storedP);
            float3 sampleN=(Passthrough&1u)!=0u?storedN:SRGBToLinear(storedN);
            bool sampleValid=IsFiniteModelStoredV2(storedP)&&IsFiniteModelStoredV2(storedN)&&
                all(isfinite(sampleP))&&all(isfinite(sampleN));
            [branch] if(sampleValid) {
                float weight=spatial*ColourLabGuideWeight(reference,sampleP);
                sumP+=sampleP*weight;sumN+=sampleN*weight;sumW+=weight;
            } else invalid=1u;
        }
    }
    valid=invalid==0u&&isfinite(sumW)&&sumW>0.0;
    float denominator=valid?sumW:1.0;
    proxy=sumP/denominator;neural=sumN/denominator;
    valid=valid&&all(isfinite(proxy))&&all(isfinite(neural));
}
#endif
