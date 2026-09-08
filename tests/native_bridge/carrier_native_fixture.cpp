// A real DLL with controlled native NGX-shaped exports, never an NVIDIA model.
#include <windows.h>
#include <atomic>
struct ID3D11Resource;
struct ID3D12Resource;
using NVSDK_NGX_Result = int;
#include "src/interop/nr_ngx_parameter_abi.h"
static unsigned handle_storage;
static std::atomic<unsigned> creates, evaluates, releases;
static HANDLE entered, proceed, released;
extern "C" __declspec(dllexport) int NVSDK_NGX_D3D11_CreateFeature(
    void *, int, NVSDK_NGX_Parameter *p, void **out) {
    ++creates;
    if (p) p->Set("DLSS.Feature.Create.Flags", 999u);
    if (out) *out = &handle_storage;
    return 1;
}
extern "C" __declspec(dllexport) int NVSDK_NGX_D3D11_ReleaseFeature(void *) {
    ++releases;
    if (released) SetEvent(released);
    return 1;
}
extern "C" __declspec(dllexport) int NVSDK_NGX_D3D11_EvaluateFeature(void *, void *, void *, void *) {
    ++evaluates;
    if (entered) {
        SetEvent(entered);
        if (WaitForSingleObject(proceed, 5000) != WAIT_OBJECT_0) return -2;
    }
    return 1;
}
extern "C" __declspec(dllexport) void CarrierTestBarrier(HANDLE in, HANDLE go, HANDLE out) {
    entered = in; proceed = go; released = out;
}
extern "C" __declspec(dllexport) unsigned CarrierTestCount(unsigned which) {
    return which == 0 ? creates.load() : which == 1 ? evaluates.load() : releases.load();
}
