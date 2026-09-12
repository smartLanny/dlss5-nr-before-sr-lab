#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS

#include <windows.h>
#include <d3d12.h>
#include <shlobj.h>
#include <nvsdk_ngx.h>

#include <cstdint>

namespace {
using NgxResult = std::uint32_t;
volatile LONG g_post_call_epoch = 0;
}

// The official SDK loader discovers the driver core. Do not emulate its
// registry/path policy or provide a home-grown NVSDK_NGX_Parameter vtable.
// Called only when the host has not loaded NGX; no SR feature is created.
extern "C" __declspec(dllexport) NgxResult NRChain_Bootstrap(ID3D12Device *device) {
    if (!device) return static_cast<NgxResult>(NVSDK_NGX_Result_FAIL_InvalidParameter);
    wchar_t directory[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                              nullptr, SHGFP_TYPE_CURRENT, directory)))
        return static_cast<NgxResult>(NVSDK_NGX_Result_Fail);
    if (wcscat_s(directory, L"\\DLSS5") != 0) return static_cast<NgxResult>(NVSDK_NGX_Result_Fail);
    if (!CreateDirectoryW(directory, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return static_cast<NgxResult>(NVSDK_NGX_Result_Fail);
    if (wcscat_s(directory, L"\\NGX") != 0) return static_cast<NgxResult>(NVSDK_NGX_Result_Fail);
    if (!CreateDirectoryW(directory, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return static_cast<NgxResult>(NVSDK_NGX_Result_Fail);
    // A plugin-owned identifier, not an invented NVIDIA-assigned application ID.
    const auto result = NVSDK_NGX_D3D12_Init_with_ProjectID(
        "1e962a93-7a44-4b5f-93b6-9b1a87896f15", NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        "DLSS5-0.5", directory, device, nullptr, NVSDK_NGX_Version_API);
    InterlockedIncrement(&g_post_call_epoch);
    // Keep this SDK instance and module alive with the process. Shutdown here
    // could invalidate parameters/features, including a later host NGX user.
    return static_cast<NgxResult>(result);
}

// These wrappers deliberately live in a module whose leaf name contains
// "nvngx.dll". Each target invocation is a real CALL followed by an observable
// operation, so the compiler cannot turn it into a tail jump. The signed NR
// snippet therefore sees a normal NGX-family caller return address without any
// patch to NVIDIA code.

extern "C" __declspec(dllexport) NgxResult NRChain_Init(
    void *target, unsigned long long application_id,
    const wchar_t *application_data_path, ID3D12Device *device,
    std::uint32_t sdk_version, const void *parameters) {
    using Fn = NgxResult (*)(unsigned long long, const wchar_t *, ID3D12Device *,
                             std::uint32_t, const void *);
    const NgxResult result = reinterpret_cast<Fn>(target)(
        application_id, application_data_path, device, sdk_version, parameters);
    InterlockedIncrement(&g_post_call_epoch);
    return result;
}

extern "C" __declspec(dllexport) NgxResult NRChain_Create(
    void *target, ID3D12GraphicsCommandList *command_list, int feature_id,
    void *parameters, void **out_handle) {
    using Fn = NgxResult (*)(ID3D12GraphicsCommandList *, int, void *, void **);
    const NgxResult result = reinterpret_cast<Fn>(target)(
        command_list, feature_id, parameters, out_handle);
    InterlockedIncrement(&g_post_call_epoch);
    return result;
}

extern "C" __declspec(dllexport) NgxResult NRChain_Evaluate(
    void *target, ID3D12GraphicsCommandList *command_list,
    const void *handle, const void *parameters, void *callback) {
    using Fn = NgxResult (*)(ID3D12GraphicsCommandList *, const void *,
                             const void *, void *);
    const NgxResult result = reinterpret_cast<Fn>(target)(
        command_list, handle, parameters, callback);
    InterlockedIncrement(&g_post_call_epoch);
    return result;
}

extern "C" __declspec(dllexport) NgxResult NRChain_Release(
    void *target, void *handle) {
    using Fn = NgxResult (*)(void *);
    const NgxResult result = reinterpret_cast<Fn>(target)(handle);
    InterlockedIncrement(&g_post_call_epoch);
    return result;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
    return TRUE;
}
