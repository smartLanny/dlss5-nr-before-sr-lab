#include "contract.hpp"
#include <atomic>
#include <cassert>
#include <cstring>
#include <cwchar>
#include <cstdio>
#include "MinHook.h"
std::atomic<HMODULE> g_addon_module{nullptr};
std::atomic<bool> g_core_lifecycle_hooks_ready{false};
thread_local unsigned g_private_nr_call_depth=0;
constexpr NgxResult kNgxFeatureNotFound=0xBAD00004u;
static HMODULE core_module=nullptr;
static CreateFn native_create=nullptr;
static bool hook_failure=false;
void Log(const char*,...) {}
bool PinHookAddress(const void* address,const char*) {
    HMODULE m=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(address),&m)!=FALSE;
}
void PollNgxLifecycleModules(bool force);
#include "../../src/nr_core_create_boundary.inl"
NgxResult ObserveCreate(ID3D12GraphicsCommandList* l,int f,NgxParams* p,NgxHandle** out) {
    ++p->core_entries;return native_create(l,f,p,out);
}
void PollNgxLifecycleModules(bool force) {
    assert(force);
    if(g_core_lifecycle_hooks_ready.load())return;
    auto target=GetProcAddress(core_module,"NVSDK_NGX_D3D12_CreateFeature");
    // Own lookups must never receive the dispatch thunk.
    if(!target||target==reinterpret_cast<FARPROC>(&CoreCreateBeforeNative)) {hook_failure=true;return;}
    auto r=MH_Initialize();if(r!=MH_OK&&r!=MH_ERROR_ALREADY_INITIALIZED){hook_failure=true;return;}
    r=MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&ObserveCreate),
        reinterpret_cast<void**>(&native_create));
    if(r!=MH_OK){hook_failure=true;return;}
    r=MH_EnableHook(reinterpret_cast<void*>(target));
    if(r!=MH_OK&&r!=MH_ERROR_ENABLED){hook_failure=true;return;}
    g_core_lifecycle_hooks_ready=true;
}
void ModelPreflight(){PollNgxLifecycleModules(true);}
int wmain(int argc,wchar_t** argv) {
    if(argc<3)return 2;
    const bool baseline=argc>3&&std::wcscmp(argv[3],L"--baseline")==0;
    g_addon_module=GetModuleHandleW(nullptr);
    core_module=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    auto client=LoadLibraryExW(argv[2],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!core_module||!client)return 3;
    if(!baseline&&!InstallCoreCreateBoundaryHook())return 4;
    using RunFn=NgxResult (*)(HMODULE,NgxParams*,NgxHandle**);
    auto run=reinterpret_cast<RunFn>(GetProcAddress(client,"RunCreate"));if(!run)return 5;
    NgxParams p;NgxHandle* out=nullptr;p.model_preflight=&ModelPreflight;
    const auto result=run(core_module,&p,&out);
    if(hook_failure||result!=1||p.native_calls!=1||p.model_entries!=1||out!=&p.handle)return 6;
    if(baseline){
        if(p.core_entries!=0||p.covered_at_native_entry)return 7;
        std::puts("old model-preflight: core Create entry missed (reproduced)");return 11;
    }
    if(p.core_entries!=1||!p.covered_at_native_entry||g_core_boundary_entries!=1||g_core_boundary_covered!=1)return 8;
    if(run(core_module,&p,&out)!=1||p.native_calls!=2||p.core_entries!=2||g_core_boundary_entries!=1)return 9;
    std::puts("real Win32 resolver/MinHook boundary: early Core entry observed, native called once per request");
    return 0;
}
