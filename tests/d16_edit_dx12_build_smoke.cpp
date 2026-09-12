#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "nr_edit_dx12_dispatch.hpp"
#include <cstdio>
int wmain(int argc,wchar_t** argv){
    if(argc!=3){std::fprintf(stderr,"usage: smoke rr|deferred shader-path\n");return 2;}
    IDXGIFactory4* factory=nullptr;IDXGIAdapter* adapter=nullptr;ID3D12Device* device=nullptr;
    auto hr=CreateDXGIFactory2(0,IID_PPV_ARGS(&factory));
    if(SUCCEEDED(hr))hr=factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
    if(SUCCEEDED(hr))hr=D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device));
    if(adapter)adapter->Release();if(factory)factory->Release();
    if(FAILED(hr)){std::fprintf(stderr,"WARP device HRESULT=0x%08X\n",unsigned(hr));return 3;}
    const bool rr=wcscmp(argv[1],L"rr")==0;
    const char* entries[]={rr?"PrepareGuidesCS":"EncodeCS",rr?"AccumulateCS":"ComposeCS","ComposeCS"};
    bool ok=true;for(unsigned i=0;i<(rr?3u:2u);++i){char error[256]{};
        auto* p=nr_edit::dx12::Create(device,argv[2],entries[i],error,sizeof(error));
        if(!p){std::fprintf(stderr,"%s: %s\n",entries[i],error);ok=false;break;}
        if(!nr_edit::dx12::DestroyAfterRetirement(p)){ok=false;break;}
    }
    device->Release();if(ok)std::puts("WARP pipeline/descriptor/constant-buffer construction passed. No shader dispatch, NVIDIA or game execution.");
    return ok?0:4;
}
