// Shared signed residual carrier. No colour transfer or enhancement controls.
float NearestCarrierHalf(float value){
    uint low=f32tof16(value);
    if(low>0&&f16tof32(low)>value)--low;
    float a=f16tof32(low),b=f16tof32(low+1);
    float da=value-a,db=b-value;
    return db<da||(db==da&&(low&1))?b:a;
}
void EncodeResidualCarrier(float3 delta,bool valid,float range,out float4 carrier,out float4 tail){
    float3 bounded=clamp(delta,-range,range);
    float3 encoded=.5+.5*bounded/range;
    float3 stored=float3(NearestCarrierHalf(encoded.x),NearestCarrierHalf(encoded.y),NearestCarrierHalf(encoded.z));
    float3 represented=(2*stored-1)*range;
    carrier=valid?float4(stored,1):float4(.5,.5,.5,1);
    // 0 invalid, 1 exact tail only, 2 a non-neutral FP16 carrier exists.
    tail=valid?float4(delta-represented,any(represented!=0)?2:1):float4(0,0,0,0);
}
