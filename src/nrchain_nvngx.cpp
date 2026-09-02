#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS

#include <windows.h>
#include <d3d12.h>

#include <cstdint>

namespace {
using NgxResult = std::uint32_t;
volatile LONG g_post_call_epoch = 0;
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
