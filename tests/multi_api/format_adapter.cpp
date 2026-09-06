// This compiles the actual production C-interface adapter with a CPU seam.
// It is NOT the Windows SDK, a D3D12 driver, or a WARP test.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "src/interop/nr_format_access.h"
using DXGI_FORMAT = std::uint32_t;
constexpr DXGI_FORMAT DXGI_FORMAT_UNKNOWN=0;
constexpr DXGI_FORMAT DXGI_FORMAT_R32G32B32A32_TYPELESS=1;
constexpr DXGI_FORMAT DXGI_FORMAT_R32G32B32A32_FLOAT=2;
constexpr DXGI_FORMAT DXGI_FORMAT_R16G16B16A16_TYPELESS=9;
constexpr DXGI_FORMAT DXGI_FORMAT_R16G16B16A16_FLOAT=10;
using HRESULT=std::int32_t;
#define FAILED(x) ((x)<0)
constexpr unsigned D3D12_RESOURCE_DIMENSION_TEXTURE2D=3;
constexpr unsigned D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE=8;
constexpr unsigned D3D12_FEATURE_FORMAT_SUPPORT=3;
constexpr unsigned D3D12_FORMAT_SUPPORT1_TEXTURE2D=0x20;
constexpr unsigned D3D12_FORMAT_SUPPORT1_SHADER_LOAD=0x100;
constexpr unsigned D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW=0x2000000;
constexpr unsigned D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE=0x80;
struct D3D12_RESOURCE_DESC { unsigned Dimension=3,DepthOrArraySize=1,MipLevels=1;
    struct {unsigned Count=1;} SampleDesc; unsigned Flags=0; DXGI_FORMAT Format=10; };
struct D3D12_FEATURE_DATA_FORMAT_SUPPORT {DXGI_FORMAT Format=0;unsigned Support1=0,Support2=0;};
struct ID3D12Device {bool fail=false,load=true,texture=true;unsigned calls=0;DXGI_FORMAT queried=0;};
HRESULT ID3D12Device_CheckFeatureSupport(ID3D12Device* dev,unsigned feature,
        D3D12_FEATURE_DATA_FORMAT_SUPPORT* support,unsigned size) {
    if(feature!=D3D12_FEATURE_FORMAT_SUPPORT || size!=sizeof(*support)) std::abort();
    ++dev->calls;dev->queried=support->Format;
    if(dev->fail)return -1;
    support->Support1=(dev->texture?D3D12_FORMAT_SUPPORT1_TEXTURE2D:0) |
        (dev->load?D3D12_FORMAT_SUPPORT1_SHADER_LOAD:0);
    support->Support2=0; // no UAV support is necessary to read pre-SR SceneColor
    return 0;
}
#include "src/interop/nr_format_access_d3d12.inl"
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<" " #x "\n";std::exit(1);}}while(0)
int main() {
    ID3D12Device dev;D3D12_RESOURCE_DESC desc;
    for (unsigned f : {1u,2u,9u,10u,26u}) {
        desc.Format=f;
        auto view=NrResolveSceneSrv(&dev,desc);
        CHECK(view==NrSceneReadFormat(f));
        CHECK(view==dev.queried);
        CHECK(NrIsFloatSceneStorage(f));
        CHECK(nr_format_access::LegalView(f,view));
    }
    desc.Format=1; dev.fail=true;
    CHECK(NrResolveSceneSrv(&dev,desc)==0);
    dev.fail=false; CHECK(NrResolveSceneSrv(&dev,desc)==2);
    dev.load=false;CHECK(NrResolveSceneSrv(&dev,desc)==0);
    dev.load=true;dev.texture=false;CHECK(NrResolveSceneSrv(&dev,desc)==0);
    dev.texture=true;
    desc.Flags=8;unsigned before=dev.calls;CHECK(NrResolveSceneSrv(&dev,desc)==0);CHECK(dev.calls==before);
    desc.Flags=0;desc.SampleDesc.Count=4;CHECK(NrResolveSceneSrv(&dev,desc)==0);
    desc.SampleDesc.Count=1;desc.MipLevels=4;CHECK(NrResolveSceneSrv(&dev,desc)==0);
    desc.MipLevels=1;desc.DepthOrArraySize=2;CHECK(NrResolveSceneSrv(&dev,desc)==0);
    desc.DepthOrArraySize=1;desc.Dimension=1;CHECK(NrResolveSceneSrv(&dev,desc)==0);
    desc.Dimension=3;CHECK(NrResolveSceneSrv(nullptr,desc)==0);
    desc.Format=3;CHECK(NrResolveSceneSrv(&dev,desc)==0); // UINT not float4
    desc.Format=27;CHECK(NrResolveSceneSrv(&dev,desc)==0); // no guessed gamma/UNORM
    std::cout<<checks<<" actual adapter host-seam checks passed; no Windows/GPU claim\n";
}
