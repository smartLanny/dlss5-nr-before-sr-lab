#pragma once
// Reads the same typed mip-0 view that the model/compositor consumes. No
// guessed state transitions on game-owned depth/MV textures are necessary.
constexpr const char kNrReplayShader[] = R"hlsl(
Texture2D<float4> Image : register(t0);
RWByteAddressBuffer Payload : register(u0);
cbuffer CopyShape : register(b0) {
    uint Width; uint Height; uint Channels; uint Half;
    uint Offset; uint Pad0; uint Pad1; uint Pad2;
};
[numthreads(8,8,1)]
void ReplayCopyCS(uint3 id : SV_DispatchThreadID) {
    if(id.x>=Width || id.y>=Height)return;
    float4 v=Image.Load(int3(id.xy+uint2(Pad1,Pad2),0));
    uint pixel=id.y*Width+id.x;
    uint at=Offset+pixel*Channels*(Half!=0 ? 2 : 4);
    if(Half!=0) {
        uint4 h=f32tof16(v);
        if(Channels==4)Payload.Store2(at,uint2(h.x|(h.y<<16),h.z|(h.w<<16)));
        else Payload.Store(at,h.x|(h.y<<16));
    } else if(Channels==4)Payload.Store4(at,asuint(v));
    else if(Channels==2)Payload.Store2(at,asuint(v.xy));
    else Payload.Store(at,asuint(v.x));
    if(id.x+1==Width && id.y+1==Height) {
        uint end=Offset+Width*Height*Channels*(Half!=0 ? 2 : 4);
        uint aligned=min((end+15)&~15u,Pad0);
        for(uint pad=end;pad<aligned;pad+=4)Payload.Store(pad,0);
    }
}
)hlsl";
