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
// Receipts distinguish successful original Init from original Core Create. Shutdown invalidates
// before forwarding, including failures: uncertainty must never reuse an epoch.
enum class DomainEvidence:unsigned {None,OriginalInit,ObservedInitializedDomain};
struct InitDomain {ID3D12Device* device=nullptr;HMODULE core=nullptr;unsigned long long epoch=0;DomainEvidence evidence=DomainEvidence::None;};
struct ParentDomain {const NgxHandle* handle=nullptr;unsigned long long generation=0,epoch=0;void* create_export=nullptr;};
ParentDomain parent_domains[128]{};
SRWLOCK init_domain_lock=SRWLOCK_INIT;
InitDomain init_domains[4]{};unsigned long long next_init_epoch=0;
std::atomic<bool> shutdown_hooks_covered{false};
std::atomic<unsigned long long> shutdown_sequence{0};
std::atomic<unsigned> shutdown_inflight{0};
unsigned long long CaptureDomainTicket(){
    AcquireSRWLockShared(&init_domain_lock);
    const auto ticket=shutdown_inflight.load()?UINT64_MAX:shutdown_sequence.load();
    ReleaseSRWLockShared(&init_domain_lock);return ticket;
}
void ObserveInit(ID3D12Device* device,NgxResult result,DomainEvidence evidence=DomainEvidence::OriginalInit,unsigned long long ticket=UINT64_MAX){
    if(result!=kNgxSuccess||!device||!shutdown_hooks_covered.load())return;
    const auto core=GetModuleHandleW(L"_nvngx.dll");if(!core)return;
    AcquireSRWLockExclusive(&init_domain_lock);
    if(shutdown_inflight.load()||ticket!=shutdown_sequence.load()){ReleaseSRWLockExclusive(&init_domain_lock);return;}
    InitDomain* selected=nullptr;
    for(auto& d:init_domains)if(d.device&&IsSameRrDepthDevice(d.device,device)){selected=&d;break;}
    if(!selected)for(auto& d:init_domains)if(!d.device){selected=&d;break;}
    if(selected){if(!selected->device){ID3D12Device_AddRef(device);selected->device=device;}
        if(evidence==DomainEvidence::OriginalInit||!selected->epoch){
            for(auto& p:parent_domains)if(p.epoch==selected->epoch)p={};
            selected->epoch=++next_init_epoch;}
        selected->core=core;selected->evidence=evidence;}
    ReleaseSRWLockExclusive(&init_domain_lock);
}
void InvalidateInit(ID3D12Device* device){
    AcquireSRWLockExclusive(&init_domain_lock);shutdown_sequence.fetch_add(1);
    for(auto& d:init_domains)if(d.device&&(!device||IsSameRrDepthDevice(d.device,device))){
        for(auto& p:parent_domains)if(p.epoch==d.epoch)p={};
        ID3D12Device_Release(d.device);d={};}
    ReleaseSRWLockExclusive(&init_domain_lock);
}
unsigned long long DomainEpoch(ID3D12Device* device,HMODULE core){
    unsigned long long epoch=0;if(!shutdown_hooks_covered.load()||shutdown_inflight.load())return 0;
    AcquireSRWLockShared(&init_domain_lock);
    for(const auto& d:init_domains)if(!shutdown_inflight.load()&&d.device&&d.core==core&&IsSameRrDepthDevice(d.device,device)){epoch=d.epoch;break;}
    ReleaseSRWLockShared(&init_domain_lock);return epoch;
}
// Called only after an actual original Core Create returns success. This proves
// an initialized central factory without creating an SR parent or issuing Init.
void ObserveCoreCreate(ID3D12GraphicsCommandList* list,int feature,const NgxHandle* handle,NgxResult result,unsigned long long ticket){
    if(InPrivateCall()||result!=kNgxSuccess||!handle||!list||(feature!=1&&feature!=13))return;
    ID3D12Device* device=nullptr;
    if(FAILED(ID3D12GraphicsCommandList_GetDevice(list,IID_ID3D12Device,reinterpret_cast<void**>(&device)))||!device)return;
    const auto core=GetModuleHandleW(L"_nvngx.dll");
    auto* target=core?reinterpret_cast<void*>(GetProcAddress(core,"NVSDK_NGX_D3D12_CreateFeature")):nullptr;
    HMODULE actual_owner=nullptr;
    if(!target||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(target),&actual_owner)||actual_owner!=core){ID3D12Device_Release(device);return;}
    ObserveInit(device,result,DomainEvidence::ObservedInitializedDomain,ticket);
    const auto epoch=DomainEpoch(device,core);ID3D12Device_Release(device);if(!epoch)return;
    AcquireSRWLockExclusive(&init_domain_lock);
    if(shutdown_inflight.load()||ticket!=shutdown_sequence.load()){ReleaseSRWLockExclusive(&init_domain_lock);return;}
    ParentDomain* selected=nullptr;
    for(auto& p:parent_domains)if(p.handle==handle){selected=&p;break;}
    if(!selected)for(auto& p:parent_domains)if(!p.handle){selected=&p;break;}
    if(selected)*selected={handle,0,epoch,target};
    ReleaseSRWLockExclusive(&init_domain_lock);
}
void SealCoreCreate(const NgxHandle* handle,NgxResult result){
    if(InPrivateCall()||result!=kNgxSuccess||!handle)return;
    const auto native=FindFeature(handle);if(!native.generation||native.pending||native.retiring)return;
    AcquireSRWLockExclusive(&init_domain_lock);
    ParentDomain alias{};
    for(auto& p:parent_domains)if(p.handle){
        FeatureRecord child{};{ScopedNgxLayer layer(NgxLayer::Core);child=FindFeature(p.handle);}
        if(child.generation==native.generation&&child.context_handle==native.context_handle&&!child.pending&&!child.retiring){
            p.generation=native.generation;alias=p;}
    }
    if(alias.handle&&alias.handle!=handle){
        ParentDomain* selected=nullptr;
        for(auto& p:parent_domains)if(p.handle==handle){selected=&p;break;}
        if(!selected)for(auto& p:parent_domains)if(!p.handle){selected=&p;break;}
        if(selected){*selected=alias;selected->handle=handle;}
    }
    ReleaseSRWLockExclusive(&init_domain_lock);
}
void ForgetParent(const NgxHandle* handle){
    AcquireSRWLockExclusive(&init_domain_lock);unsigned long long generation=0,epoch=0;
    for(const auto& p:parent_domains)if(p.handle==handle){generation=p.generation;epoch=p.epoch;break;}
    for(auto& p:parent_domains)if(p.handle==handle||(generation&&p.generation==generation&&p.epoch==epoch))p={};
    ReleaseSRWLockExclusive(&init_domain_lock);
}
bool ParentInDomain(const NgxHandle* handle,unsigned long long generation,unsigned long long epoch,void* target){
    bool found=false;AcquireSRWLockShared(&init_domain_lock);
    for(const auto& p:parent_domains)if(p.handle==handle&&p.generation==generation&&p.epoch==epoch&&p.create_export==target){found=true;break;}
    ReleaseSRWLockShared(&init_domain_lock);return found;
}
struct Exports {
    ID3D12Device* domain_device=nullptr;HMODULE domain_core=nullptr;unsigned long long domain_epoch=0;
    Allocate allocate=nullptr;Destroy destroy=nullptr;Create create=nullptr;
    Evaluate evaluate=nullptr;Release release=nullptr;HMODULE retained_modules[5]{};
    bool verified_sr_runtime_and_factory=false,all_hook_entries_have_private_bypass=false;
};
bool DomainAlive(const Exports& e){return e.domain_epoch&&DomainEpoch(e.domain_device,e.domain_core)==e.domain_epoch;}
void ReleaseExports(Exports& e){for(auto& m:e.retained_modules)if(m){FreeLibrary(m);m=nullptr;}e={};}
bool ResolveExports(Exports& e,ID3D12Device* device,const NgxHandle* parent,unsigned long long generation) {
#if !defined(NR_EDIT_PRIVATE_SR_BYPASS_COMPLETE)
#error Private SR requires all existing NGX hook bypasses before use.
#endif
    const auto native=FindFeature(parent);
    if(!device||!g_codec.device||!IsSameRrDepthDevice(device,g_codec.device)||
       native.handle!=parent||(native.feature_id!=1&&native.feature_id!=13)||native.generation!=generation||native.pending||native.retiring)return false;
    if(e.verified_sr_runtime_and_factory)return DomainAlive(e);
    const auto core=GetModuleHandleW(L"_nvngx.dll");if(!core)return false;
    const auto epoch=DomainEpoch(device,core);if(!epoch||!ParentInDomain(parent,generation,epoch,
        reinterpret_cast<void*>(GetProcAddress(core,"NVSDK_NGX_D3D12_CreateFeature"))))return false;
    const char* names[]={"NVSDK_NGX_D3D12_CreateFeature","NVSDK_NGX_D3D12_EvaluateFeature",
        "NVSDK_NGX_D3D12_ReleaseFeature","NVSDK_NGX_D3D12_AllocateParameters","NVSDK_NGX_D3D12_DestroyParameters"};
    FARPROC functions[5]{};
    for(unsigned i=0;i<5;i++){
        functions[i]=GetProcAddress(core,names[i]);
        if(!functions[i]||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(functions[i]),&e.retained_modules[i])||e.retained_modules[i]!=core){ReleaseExports(e);return false;}
    }
    e.domain_device=device;e.domain_core=core;e.domain_epoch=epoch;
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
    if(!DomainAlive(e)||!e.verified_sr_runtime_and_factory||!e.all_hook_entries_have_private_bypass||
       !e.retained_modules[0]||!e.allocate||!e.destroy||!e.create||!e.evaluate||!e.release)return a;
    a.user=&e;a.original_sr_owner_verified=true;
    a.allocate=[](void* u,void** p)->std::uint32_t{ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;if(!DomainAlive(*static_cast<Exports*>(u)))return 0xBAD00005u;NgxParams* block=nullptr;
        const auto r=static_cast<Exports*>(u)->allocate(&block);*p=block;return r;};
    a.destroy=[](void* u,void* p)->std::uint32_t{ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;
        if(!DomainAlive(*static_cast<Exports*>(u)))return 0xBAD00005u;
        return static_cast<Exports*>(u)->destroy(static_cast<NgxParams*>(p));};
    a.set_u=[](void* u,void* p,const char* k,unsigned v){ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;
        if(DomainAlive(*static_cast<Exports*>(u)))SetUInt(static_cast<NgxParams*>(p),k,v);};
    a.set_i=[](void* u,void* p,const char* k,int v){ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;
        if(DomainAlive(*static_cast<Exports*>(u)))SetInt(static_cast<NgxParams*>(p),k,v);};
    a.set_f=[](void* u,void* p,const char* k,float v){ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;
        if(DomainAlive(*static_cast<Exports*>(u)))SetFloat(static_cast<NgxParams*>(p),k,v);};
    a.set_r=[](void* u,void* p,const char* k,void* v){ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;
        if(DomainAlive(*static_cast<Exports*>(u)))SetResource(static_cast<NgxParams*>(p),k,static_cast<ID3D12Resource*>(v));};
    a.create=[](void* u,void* l,int feature,void* p,void** h)->std::uint32_t{ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;
        if(!DomainAlive(*static_cast<Exports*>(u))||feature!=1)return 0xBAD00005u;
        NgxHandle* created=nullptr;const auto r=static_cast<Exports*>(u)->create(static_cast<ID3D12GraphicsCommandList*>(l),1,static_cast<NgxParams*>(p),&created);
        *h=created;return r;};
    a.evaluate=[](void* u,void* l,void* h,void* p)->std::uint32_t{ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;
        if(!DomainAlive(*static_cast<Exports*>(u)))return 0xBAD00005u;
        return static_cast<Exports*>(u)->evaluate(static_cast<ID3D12GraphicsCommandList*>(l),static_cast<NgxHandle*>(h),static_cast<NgxParams*>(p),nullptr);};
    a.release=[](void* u,void* h)->std::uint32_t{ScopedSrwExclusive lock(&g_nr_api_lock);Scope s;
        if(!DomainAlive(*static_cast<Exports*>(u)))return 0xBAD00005u;
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
