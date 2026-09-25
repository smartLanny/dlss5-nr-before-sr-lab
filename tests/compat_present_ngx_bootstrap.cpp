// Official SDK initialization only. No Feature18 substitute or driver patch.
#include <windows.h>
#include <d3d12.h>
#include <nvsdk_ngx.h>
extern "C" __declspec(dllexport) unsigned HostInitializeNgx(ID3D12Device* device,const wchar_t* directory) {
    return static_cast<unsigned>(NVSDK_NGX_D3D12_Init(0x1000000ULL,directory,device,nullptr,NVSDK_NGX_Version_API));
}
extern "C" __declspec(dllexport) unsigned HostShutdownNgx(ID3D12Device* device) {
    return static_cast<unsigned>(NVSDK_NGX_D3D12_Shutdown1(device));
}
