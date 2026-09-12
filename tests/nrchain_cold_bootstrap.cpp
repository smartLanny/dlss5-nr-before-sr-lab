// No link to NGX SDK: the production bridge alone must bootstrap a cold process.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <nvsdk_ngx.h>
#include <cstdio>
#include <cwchar>

int wmain(int argc, wchar_t **argv) {
    if (argc != 2 || GetModuleHandleW(L"_nvngx.dll")) return 2;
    IDXGIFactory4 *factory = nullptr;
    IDXGIAdapter1 *adapter = nullptr;
    ID3D12Device *device = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return 3;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{}; adapter->GetDesc1(&d);
        if (d.VendorId == 0x10de && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) break;
        adapter->Release(); adapter = nullptr;
    }
    if (!adapter || FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return 4;
    if (GetModuleHandleW(L"_nvngx.dll")) return 5;
    const auto bridge = LoadLibraryExW(argv[1], nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!bridge || GetModuleHandleW(L"_nvngx.dll")) return 6;
    const auto bootstrap = reinterpret_cast<unsigned (*)(ID3D12Device *)>(GetProcAddress(bridge, "NRChain_Bootstrap"));
    if (!bootstrap || bootstrap(nullptr) != static_cast<unsigned>(NVSDK_NGX_Result_FAIL_InvalidParameter)) return 7;
    const auto result = bootstrap(device);
    const auto core = GetModuleHandleW(L"_nvngx.dll");
    std::printf("coldCoreAbsent=1 hostNgxInitCalls=0 nativeSRFeatures=0 bootstrapResult=0x%08X coreLoaded=%d\n", result, core != nullptr);
    if (result != 1 || !core) return 8;
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(core, path, 32768)) return 9;
    std::wprintf(L"driverCorePath=%ls\n", path);
    const auto allocate = reinterpret_cast<NVSDK_NGX_Result (*)(NVSDK_NGX_Parameter **)>(GetProcAddress(core, "NVSDK_NGX_D3D12_AllocateParameters"));
    const auto destroy = reinterpret_cast<NVSDK_NGX_Result (*)(NVSDK_NGX_Parameter *)>(GetProcAddress(core, "NVSDK_NGX_D3D12_DestroyParameters"));
    NVSDK_NGX_Parameter *params = nullptr;
    if (!allocate || !destroy || allocate(&params) != NVSDK_NGX_Result_Success || !params) return 10;
    params->Set("Width", 1234u); unsigned width = 0;
    if (params->Get("Width", &width) != NVSDK_NGX_Result_Success || width != 1234) return 11;
    if (destroy(params) != NVSDK_NGX_Result_Success) return 12;
    std::puts("PASS real driver parameter allocation/set/get/destroy; no SDK host bootstrap or Feature1");
    device->Release(); adapter->Release(); factory->Release();
    return 0;
}
