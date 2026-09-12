#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_4.h>
#include <cstdio>
#include "src/interop/nr_bridge_baseline.h"
#pragma comment(lib,"dxgi.lib")
int main() {
    IDXGIFactory4* factory=nullptr; IDXGIAdapter* adapter=nullptr;
    ID3D12Device* device=nullptr; ID3D12CommandAllocator* allocator=nullptr;
    ID3D12GraphicsCommandList* list=nullptr;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
       FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) ||
       FAILED(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))) ||
       FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))) ||
       FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&list)))) return 1;
    if(nr_bridge_baseline::Bind(nullptr) || !nr_bridge_baseline::Bind(list) || !nr_bridge_baseline::Bind(list)) return 2;
    if(FAILED(list->Close()) || FAILED(allocator->Reset()) || FAILED(list->Reset(allocator,nullptr)) ||
       !nr_bridge_baseline::Bind(list) || FAILED(list->Close())) return 3;
    // No submission is made. This tests the actual D3D12 helper, not NGX or
    // ReShade state restoration, and deliberately does not claim otherwise.
    list->Release(); allocator->Release(); device->Release(); adapter->Release(); factory->Release();
    std::puts("WARP private baseline compile/create/rebind/reset passed (no NGX/game validation)");
    return 0;
}
