#pragma once
#include <d3d12.h>
#include <cstdint>
#include <limits>

// Mirrors the six guide planes in Capture and nine backend planes in
// nr_post_private_sr_dx12::Create. Query the same device and exact texture
// descriptions before allocating any of them or entering opaque NGX Create.
namespace nr_private_sr_budget {
struct Plan { bool valid=false; std::uint64_t guides=0,backend=0,required=0; };
inline Plan Query(ID3D12Device* device,unsigned nw,unsigned nh,unsigned ow,unsigned oh) {
    Plan p{};
    if(!device||!nw||!nh||!ow||!oh||nw>16384||nh>16384||ow>16384||oh>16384)return p;
    constexpr unsigned formats[]={41,16,10,2,10};
    for(unsigned i=0;i<5;++i) {
        D3D12_RESOURCE_DESC d{};
        d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width=i==4?ow:nw;d.Height=i==4?oh:nh;
        d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
        d.Format=static_cast<DXGI_FORMAT>(formats[i]);
        d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        const auto a=ID3D12Device_GetResourceAllocationInfo(device,0,1,&d);
        if(!a.SizeInBytes||a.SizeInBytes==UINT64_MAX||
           a.SizeInBytes>std::numeric_limits<std::uint64_t>::max()/3)return {};
        const auto tripled=a.SizeInBytes*3;
        auto& subtotal=i<2?p.guides:p.backend;
        if(tripled>std::numeric_limits<std::uint64_t>::max()-subtotal)return {};
        subtotal+=tripled;
    }
    if(p.backend>std::numeric_limits<std::uint64_t>::max()-p.guides)return {};
    p.required=p.guides+p.backend;p.valid=true;return p;
}
}
