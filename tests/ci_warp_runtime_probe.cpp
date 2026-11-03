// Real Windows loader/device probe for the CI-only helper. No shader or GPU
// workload gate is replaced by this small process-boundary contract.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "ci_warp_runtime.h"

static bool Ok(HRESULT result, const char* operation) {
    if (SUCCEEDED(result)) return true;
    std::fprintf(stderr, "FAIL runtime-probe operation=%s hresult=0x%08lx\n",
                 operation, static_cast<unsigned long>(result));
    return false;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--system-first") == 0) {
        wchar_t directory[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return 10;
        const std::wstring system_warp = std::wstring(directory) + L"\\d3d10warp.dll";
        // Deliberately retained in this isolated negative-control process.
        if (!LoadLibraryExW(system_warp.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) return 11;
    } else if (argc != 1) {
        return 12;
    }
    if (!Ok(nr_test_warp::Prepare(), "prepare")) return 2;
    IDXGIFactory4* factory = nullptr;
    IDXGIAdapter1* warp = nullptr;
    ID3D12Device* device = nullptr;
    if (!Ok(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "factory")) return 3;
    const HRESULT adapter_result = factory->EnumWarpAdapter(IID_PPV_ARGS(&warp));
    factory->Release();
    if (!Ok(adapter_result, "warp-adapter")) return 4;
    DXGI_ADAPTER_DESC1 description{};
    if (!Ok(warp->GetDesc1(&description), "adapter-description") ||
        (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
        warp->Release();
        return 5;
    }
    const HRESULT device_result = D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0,
                                                  IID_PPV_ARGS(&device));
    warp->Release();
    if (!Ok(device_result, "device")) return 6;
    const HRESULT verified = nr_test_warp::Verify("runtime-probe");
    device->Release();
    if (!Ok(verified, "verify")) return 7;
    std::printf("PASS CI_WARP_PROBE configured=%u\n", nr_test_warp::Configured() ? 1u : 0u);
    return 0;
}
