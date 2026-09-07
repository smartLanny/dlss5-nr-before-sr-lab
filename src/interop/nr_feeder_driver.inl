// In-process DLAA SDK forwarding for the optional, single-core Feeder backend.
// The caller uses the genuine NGX Feature 1 API. This file owns no NR model,
// no GPU queue and no NGX feature. It replaces only the SDK's static loader.
// Driver Init_Ext / Init_ProjectID argument order and registry discovery are
// based on NIGos/dlss5-bridge (MIT), commit 28aed4099b0fe1c207b20b5fee5364c0773c25c2.
// Do not catch hardware/SEH exceptions here or invent success values.
#include <mutex>
#include <string>
#include <limits>

namespace nr_feeder_driver {
struct Api {
    HMODULE module = nullptr; // retained until process exit; never unload another owner
    decltype(&NVSDK_NGX_D3D12_CreateFeature) create = nullptr;
    decltype(&NVSDK_NGX_D3D12_EvaluateFeature) evaluate = nullptr;
    decltype(&NVSDK_NGX_D3D12_EvaluateFeature_C) evaluate_c = nullptr;
    decltype(&NVSDK_NGX_D3D12_GetFeatureRequirements) requirements = nullptr;
    decltype(&NVSDK_NGX_D3D12_ReleaseFeature) release = nullptr;
    decltype(&NVSDK_NGX_D3D12_AllocateParameters) allocate = nullptr;
    decltype(&NVSDK_NGX_D3D12_DestroyParameters) destroy = nullptr;
    decltype(&NVSDK_NGX_D3D12_GetCapabilityParameters) capabilities = nullptr;
    decltype(&NVSDK_NGX_D3D12_Shutdown1) shutdown = nullptr;
    using Init = NVSDK_NGX_Result (*)(unsigned long long, const wchar_t*, ID3D12Device*,
                                     int, const NVSDK_NGX_FeatureCommonInfo*);
    using Project = NVSDK_NGX_Result (*)(const char*, int, const char*, const wchar_t*,
                                        ID3D12Device*, int, const NVSDK_NGX_FeatureCommonInfo*);
    Init init = nullptr;
    Project project = nullptr;
};
static Api api;
static std::once_flag load_once;
static bool IsAbsolute(const wchar_t* path) {
    return path && ((path[0] && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) ||
                    (path[0] == L'\\' && path[1] == L'\\'));
}
template<class T> static bool Resolve(HMODULE module, const char* name, T& out) {
    out = reinterpret_cast<T>(GetProcAddress(module, name));
    return out != nullptr;
}
static Api ResolveApi(HMODULE module) {
    Api next{};
    if (!module) return next;
    const bool complete =
        Resolve(module, "NVSDK_NGX_D3D12_CreateFeature", next.create) &&
        Resolve(module, "NVSDK_NGX_D3D12_EvaluateFeature", next.evaluate) &&
        Resolve(module, "NVSDK_NGX_D3D12_EvaluateFeature_C", next.evaluate_c) &&
        Resolve(module, "NVSDK_NGX_D3D12_ReleaseFeature", next.release) &&
        Resolve(module, "NVSDK_NGX_D3D12_AllocateParameters", next.allocate) &&
        Resolve(module, "NVSDK_NGX_D3D12_DestroyParameters", next.destroy) &&
        Resolve(module, "NVSDK_NGX_D3D12_GetCapabilityParameters", next.capabilities) &&
        Resolve(module, "NVSDK_NGX_D3D12_Shutdown1", next.shutdown) &&
        Resolve(module, "NVSDK_NGX_D3D12_Init_Ext", next.init);
    Resolve(module, "NVSDK_NGX_D3D12_Init_ProjectID", next.project); // optional alternative
    Resolve(module, "NVSDK_NGX_D3D12_GetFeatureRequirements", next.requirements); // diagnostic only
    if (complete) next.module = module;
    return next.module ? next : Api{};
}
static void Load() {
    // Read the registered driver directory, not the current directory, PATH,
    // a guessed DriverStore version, or an arbitrary DLL exporting NGX names.
    wchar_t directory[32768]{};
    DWORD bytes = sizeof(directory);
    const auto result = RegGetValueW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", L"FullPath",
        RRF_RT_REG_SZ, nullptr, directory, &bytes); // producer is x64: native registry view
    if (result != ERROR_SUCCESS || bytes < sizeof(wchar_t) ||
        bytes > sizeof(directory) || directory[(bytes / sizeof(wchar_t)) - 1] != L'\0' ||
        !IsAbsolute(directory)) {
        Log("[feed/core] registered NVIDIA NGX directory unavailable (%ld); original frame retained. F8: upload the TXT bug report.", result);
        return;
    }
    std::wstring path(directory);
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L'\\';
    path += L"_nvngx.dll";
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        Log("[feed/core] registered NGX loader failed (%lu); original frame retained. F8: upload the TXT bug report.", GetLastError());
        return;
    }
    api = ResolveApi(module);
    if (!api.module) {
        Log("[feed/core] registered NGX loader lacks required D3D12 exports; original frame retained. F8: upload the TXT bug report.");
        FreeLibrary(module); // only this unsuccessful LoadLibrary reference
        return;
    }
    Log("[feed/core] genuine Feature1 calls use registered driver %ls; no second NR owner.", path.c_str());
}
static const Api& Get() {
    std::call_once(load_once, Load);
    return api;
}
} // namespace nr_feeder_driver

// The SDK's C++ Init overload puts info before version. The native loader puts
// version before info. Keep this adapter explicit and test both call orders.
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init(unsigned long long id,
    const wchar_t* path, ID3D12Device* device, const NVSDK_NGX_FeatureCommonInfo* info,
    NVSDK_NGX_Version version) {
    const auto& a = nr_feeder_driver::Get();
    return a.module ? a.init(id, path, device, static_cast<int>(version), info) : NVSDK_NGX_Result_Fail;
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init_with_ProjectID(const char* project,
    NVSDK_NGX_EngineType engine, const char* engine_version, const wchar_t* path,
    ID3D12Device* device, const NVSDK_NGX_FeatureCommonInfo* info, NVSDK_NGX_Version version) {
    const auto& a = nr_feeder_driver::Get();
    return a.module && a.project ? a.project(project, static_cast<int>(engine), engine_version,
        path, device, static_cast<int>(version), info) : NVSDK_NGX_Result_Fail;
}
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_CreateFeature(
    ID3D12GraphicsCommandList* list, NVSDK_NGX_Feature feature, NVSDK_NGX_Parameter* params,
    NVSDK_NGX_Handle** handle) {
    const auto& a = nr_feeder_driver::Get();
    return a.module ? a.create(list, feature, params, handle) : NVSDK_NGX_Result_Fail;
}
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_EvaluateFeature(
    ID3D12GraphicsCommandList* list, const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* params, PFN_NVSDK_NGX_ProgressCallback callback) {
    const auto& a = nr_feeder_driver::Get();
    return a.module ? a.evaluate(list, handle, params, callback) : NVSDK_NGX_Result_Fail;
}
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_EvaluateFeature_C(
    ID3D12GraphicsCommandList* list, const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* params, PFN_NVSDK_NGX_ProgressCallback_C callback) {
    const auto& a = nr_feeder_driver::Get();
    return a.module ? a.evaluate_c(list, handle, params, callback) : NVSDK_NGX_Result_Fail;
}
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_GetFeatureRequirements(
    IDXGIAdapter* adapter, const NVSDK_NGX_FeatureDiscoveryInfo* info,
    NVSDK_NGX_FeatureRequirement* requirement) {
    const auto& a = nr_feeder_driver::Get();
    return a.module && a.requirements ? a.requirements(adapter, info, requirement) : NVSDK_NGX_Result_Fail;
}
// SDK C convenience functions are typed calls on the native parameter object,
// not a substitute object or fabricated values. The interface has an executable
// bidirectional Microsoft-x64-vtable oracle in tests/native_bridge.
#define NR_FEED_SET(Suffix, Type) \
extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_Set##Suffix(NVSDK_NGX_Parameter* p, const char* key, Type value) { \
    if (p && key) p->Set(key, value); \
}
#define NR_FEED_GET(Suffix, Type) \
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_Get##Suffix(NVSDK_NGX_Parameter* p, const char* key, Type* value) { \
    return p && key && value ? p->Get(key, value) : NVSDK_NGX_Result_Fail; \
}
NR_FEED_SET(ULL, unsigned long long) NR_FEED_GET(ULL, unsigned long long)
NR_FEED_SET(F, float) NR_FEED_GET(F, float)
NR_FEED_SET(D, double) NR_FEED_GET(D, double)
NR_FEED_SET(UI, unsigned int) NR_FEED_GET(UI, unsigned int)
NR_FEED_SET(I, int) NR_FEED_GET(I, int)
NR_FEED_SET(D3d11Resource, ID3D11Resource*) NR_FEED_GET(D3d11Resource, ID3D11Resource*)
NR_FEED_SET(D3d12Resource, ID3D12Resource*) NR_FEED_GET(D3d12Resource, ID3D12Resource*)
NR_FEED_SET(VoidPointer, void*) NR_FEED_GET(VoidPointer, void*)
#undef NR_FEED_SET
#undef NR_FEED_GET
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle* handle) {
    const auto& a = nr_feeder_driver::Get();
    return a.module ? a.release(handle) : NVSDK_NGX_Result_Fail;
}
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_AllocateParameters(NVSDK_NGX_Parameter** params) {
    const auto& a = nr_feeder_driver::Get();
    return a.module ? a.allocate(params) : NVSDK_NGX_Result_Fail;
}
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_DestroyParameters(NVSDK_NGX_Parameter* params) {
    const auto& a = nr_feeder_driver::Get();
    return a.module ? a.destroy(params) : NVSDK_NGX_Result_Fail;
}
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_GetCapabilityParameters(NVSDK_NGX_Parameter** params) {
    const auto& a = nr_feeder_driver::Get();
    return a.module ? a.capabilities(params) : NVSDK_NGX_Result_Fail;
}
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Shutdown1(ID3D12Device* device) {
    const auto& a = nr_feeder_driver::Get();
    return a.module ? a.shutdown(device) : NVSDK_NGX_Result_Fail;
}
