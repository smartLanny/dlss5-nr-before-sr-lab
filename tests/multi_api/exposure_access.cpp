// Executes the production D3D12 access adapter using a deterministic device seam.
// No Windows/GPU/NGX claim; assertions cover exact queried view and no UAV gate.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
using HRESULT = std::int32_t;
constexpr HRESULT E_INVALIDARG = static_cast<HRESULT>(0x80070057u);
constexpr HRESULT E_FAIL = static_cast<HRESULT>(0x80004005u);
#define SUCCEEDED(x) ((x) >= 0)
enum DXGI_FORMAT {
    DXGI_FORMAT_UNKNOWN=0, DXGI_FORMAT_R32G32B32A32_TYPELESS=1,
    DXGI_FORMAT_R32G32B32A32_FLOAT=2, DXGI_FORMAT_R32G32B32_TYPELESS=5,
    DXGI_FORMAT_R32G32B32_FLOAT=6, DXGI_FORMAT_R16G16B16A16_TYPELESS=9,
    DXGI_FORMAT_R16G16B16A16_FLOAT=10, DXGI_FORMAT_R32G32_TYPELESS=15,
    DXGI_FORMAT_R32G32_FLOAT=16, DXGI_FORMAT_R16G16_TYPELESS=33,
    DXGI_FORMAT_R16G16_FLOAT=34, DXGI_FORMAT_R32_TYPELESS=39,
    DXGI_FORMAT_R32_FLOAT=41, DXGI_FORMAT_R16_TYPELESS=53, DXGI_FORMAT_R16_FLOAT=54, DXGI_FORMAT_FORCE_UINT=0xffffffffu
};
constexpr unsigned D3D12_RESOURCE_DIMENSION_TEXTURE2D=3;
constexpr unsigned D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE=8;
constexpr unsigned D3D12_FEATURE_FORMAT_SUPPORT=3;
constexpr unsigned D3D12_FORMAT_SUPPORT1_TEXTURE2D=0x20;
constexpr unsigned D3D12_FORMAT_SUPPORT1_SHADER_LOAD=0x100;
struct D3D12_RESOURCE_DESC {
    unsigned Dimension=3;
    std::uint64_t Width=1;
    unsigned Height=1, DepthOrArraySize=1, MipLevels=1;
    struct { unsigned Count=1; } SampleDesc;
    unsigned Flags=0;
    DXGI_FORMAT Format=DXGI_FORMAT_R32_FLOAT;
};
struct D3D12_FEATURE_DATA_FORMAT_SUPPORT { DXGI_FORMAT Format{}; unsigned Support1=0, Support2=0; };
struct ID3D12Device {
    unsigned calls=0, bits=0x120;
    HRESULT hr=0;
    DXGI_FORMAT queried=DXGI_FORMAT_UNKNOWN;
};
unsigned checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
HRESULT ID3D12Device_CheckFeatureSupport(ID3D12Device *device, unsigned kind,
                                        void *data, std::size_t size) {
    CHECK(kind==D3D12_FEATURE_FORMAT_SUPPORT);
    CHECK(size==sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT));
    auto &support=*static_cast<D3D12_FEATURE_DATA_FORMAT_SUPPORT *>(data);
    CHECK(support.Support1==0 && support.Support2==0);
    device->queried=support.Format; ++device->calls;
    support.Support1=device->bits;
    support.Support2=0; // Deliberately no UAV typed load/store capability.
    return device->hr;
}
#include "src/interop/nr_exposure_access_d3d12.inl"
int main() {
    const unsigned mappings[][2]={{53,54},{54,54},{33,34},{34,34},{9,10},{10,10},
        {39,41},{41,41},{15,16},{16,16},{5,6},{6,6},{1,2},{2,2}};
    for (unsigned i=0;i<192;++i) {
        unsigned wanted=0;
        for (auto &pair:mappings) if(pair[0]==i) wanted=pair[1];
        CHECK(static_cast<unsigned>(NrExposureReadFormat(static_cast<DXGI_FORMAT>(i)))==wanted);
        for (unsigned bits: {0u,0x20u,0x100u,0x120u}) for (HRESULT hr:{HRESULT{0},HRESULT{1},E_FAIL}) {
            ID3D12Device device; device.bits=bits; device.hr=hr;
            D3D12_RESOURCE_DESC desc; desc.Format=static_cast<DXGI_FORMAT>(i);
            const auto result=NrResolveExposureAccess(&device,desc);
            CHECK(result.allowed()==(wanted!=0 && bits==0x120 && SUCCEEDED(hr)));
            CHECK(device.calls==(wanted?1u:0u));
            if(wanted) { CHECK(static_cast<unsigned>(device.queried)==wanted); CHECK(result.query_result==hr); }
            CHECK(std::strlen(NrExposureAccessReasonName(result.reason))!=0);
        }
    }
    for(auto &pair:mappings) {
        D3D12_RESOURCE_DESC good; good.Format=static_cast<DXGI_FORMAT>(pair[0]);
        for(unsigned bad=0;bad<8;++bad) {
            auto d=good; ID3D12Device dev;
            switch(bad) {
            case 0: d.Dimension=1; break;
            case 1: d.Width=0; break;
            case 2: d.Width=2; break;
            case 3: d.Height=2; break;
            case 4: d.DepthOrArraySize=2; break;
            case 5: d.MipLevels=0; break;
            case 6: d.SampleDesc.Count=4; break;
            case 7: d.Flags=8; break;
            }
            CHECK(!NrResolveExposureAccess(&dev,d).allowed()); CHECK(dev.calls==0);
        }
        ID3D12Device dev;
        CHECK(!NrResolveExposureAccess(nullptr,good).allowed());
        CHECK(NrResolveExposureAccess(&dev,good).allowed());
        CHECK(dev.calls==1); // No hidden double-query / fallback.
    }
    std::printf("%u production exposure view/capability assertions passed (CPU seam; no GPU)\n", checks);
}
