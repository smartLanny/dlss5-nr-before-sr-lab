// Included inside Core after NGX ABI/ownership helpers, not a second Init.
namespace nr_private_sr_core {
thread_local unsigned private_depth=0;
bool InPrivateCall()noexcept{return private_depth!=0;}
struct Scope {Scope(){++private_depth;}~Scope(){--private_depth;}};
using Allocate=NgxResult(*)(NgxParams**);
using Destroy=NgxResult(*)(NgxParams*);
using Create=NgxResult(*)(ID3D12GraphicsCommandList*,int,NgxParams*,NgxHandle**);
using Evaluate=NgxResult(*)(ID3D12GraphicsCommandList*,const NgxHandle*,const NgxParams*,void*);
using Release=NgxResult(*)(NgxHandle*);
struct Exports {
    Allocate allocate=nullptr;Destroy destroy=nullptr;Create create=nullptr;
    Evaluate evaluate=nullptr;Release release=nullptr;HMODULE retained_modules[5]{};
    bool verified_sr_runtime_and_factory=false,all_hook_entries_have_private_bypass=false;
};
void ReleaseExports(Exports& e){for(auto& m:e.retained_modules)if(m){FreeLibrary(m);m=nullptr;}e={};}
bool ResolveExports(Exports& e,ID3D12Device* device,const NgxHandle* parent,unsigned long long generation) {
#if !defined(NR_EDIT_PRIVATE_SR_BYPASS_COMPLETE)
#error Private SR requires all existing NGX hook bypasses before use.
#endif
    const auto native=FindFeature(parent);
    if(!device||!g_codec.device||!IsSameRrDepthDevice(device,g_codec.device)||
       native.handle!=parent||native.feature_id!=1||native.generation!=generation||native.pending||native.retiring)return false;
    if(e.verified_sr_runtime_and_factory)return true;
    const auto core=GetModuleHandleW(L"_nvngx.dll");if(!core)return false;
    const char* names[]={"NVSDK_NGX_D3D12_CreateFeature","NVSDK_NGX_D3D12_EvaluateFeature",
        "NVSDK_NGX_D3D12_ReleaseFeature","NVSDK_NGX_D3D12_AllocateParameters","NVSDK_NGX_D3D12_DestroyParameters"};
    FARPROC functions[5]{};
    for(unsigned i=0;i<5;i++){
        functions[i]=GetProcAddress(core,names[i]);
        if(!functions[i]||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(functions[i]),&e.retained_modules[i])){ReleaseExports(e);return false;}
    }
    e.create=reinterpret_cast<decltype(e.create)>(functions[0]);e.evaluate=reinterpret_cast<decltype(e.evaluate)>(functions[1]);
    e.release=reinterpret_cast<decltype(e.release)>(functions[2]);e.allocate=reinterpret_cast<decltype(e.allocate)>(functions[3]);
    e.destroy=reinterpret_cast<decltype(e.destroy)>(functions[4]);
    // Export addresses may already be hooked. Staged TLS bypasses, not the DLL
    // filename alone, keep this private feature out of game-source observation.
    e.verified_sr_runtime_and_factory=e.all_hook_entries_have_private_bypass=true;
    return true;
}
nr_private_sr::Api MakeApi(Exports& e){
    nr_private_sr::Api a{};
    if(!e.verified_sr_runtime_and_factory||!e.all_hook_entries_have_private_bypass||
       !e.retained_modules[0]||!e.allocate||!e.destroy||!e.create||!e.evaluate||!e.release)return a;
    a.user=&e;a.original_sr_owner_verified=true;
    a.allocate=[](void* u,void** p)->std::uint32_t{ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;NgxParams* block=nullptr;
        const auto r=static_cast<Exports*>(u)->allocate(&block);*p=block;return r;};
    a.destroy=[](void* u,void* p)->std::uint32_t{ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;
        return static_cast<Exports*>(u)->destroy(static_cast<NgxParams*>(p));};
    a.set_u=[](void*,void* p,const char* k,unsigned v){SetUInt(static_cast<NgxParams*>(p),k,v);};
    a.set_i=[](void*,void* p,const char* k,int v){SetInt(static_cast<NgxParams*>(p),k,v);};
    a.set_f=[](void*,void* p,const char* k,float v){SetFloat(static_cast<NgxParams*>(p),k,v);};
    a.set_r=[](void*,void* p,const char* k,void* v){SetResource(static_cast<NgxParams*>(p),k,static_cast<ID3D12Resource*>(v));};
    a.create=[](void* u,void* l,int feature,void* p,void** h)->std::uint32_t{ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;
        if(feature!=1)return 0xBAD00005u;
        NgxHandle* created=nullptr;const auto r=static_cast<Exports*>(u)->create(static_cast<ID3D12GraphicsCommandList*>(l),1,static_cast<NgxParams*>(p),&created);
        *h=created;return r;};
    a.evaluate=[](void* u,void* l,void* h,void* p)->std::uint32_t{ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;
        return static_cast<Exports*>(u)->evaluate(static_cast<ID3D12GraphicsCommandList*>(l),static_cast<NgxHandle*>(h),static_cast<NgxParams*>(p),nullptr);};
    a.release=[](void* u,void* h)->std::uint32_t{ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;
        return static_cast<Exports*>(u)->release(static_cast<NgxHandle*>(h));};
    a.retain_resource=[](void*,void* p){IUnknown_AddRef(static_cast<IUnknown*>(p));};
    a.release_resource=[](void*,void* p){IUnknown_Release(static_cast<IUnknown*>(p));};
    a.retain_fence=[](void*,std::uint64_t p){IUnknown_AddRef(reinterpret_cast<IUnknown*>(p));};
    a.release_fence=[](void*,std::uint64_t p){IUnknown_Release(reinterpret_cast<IUnknown*>(p));};
    a.poll=[](void*,nr_private_sr::Fence f)->nr_private_sr::Completion{
        if(!f)return {};return {f.queue,f.object,ID3D12Fence_GetCompletedValue(reinterpret_cast<ID3D12Fence*>(f.object))};};
    return a;
}
}
