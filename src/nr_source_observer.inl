// Source observation has its own object/recording identity. It never fabricates
// a Feature1, native Create receipt, command-state baseline, or GPU submission.
struct ObservedSourceList {
    ID3D12GraphicsCommandList* native=nullptr;
    Id lifetime=0,device=0,reset=1;
    ULONGLONG retry_after=0;
    bool ready=false,installing=false;
};
ObservedSourceList observed_lists[256]{};
std::atomic<bool> source_receipts_pending{false};
std::atomic<unsigned long long> source_observer_ready{0},source_observer_refused{0};
Recording ObservedRecording(ID3D12GraphicsCommandList* native){
    // Caller holds source_lock. This is a value lookup on recording hooks.
    for(const auto& entry:observed_lists)if(entry.native==native)
        return entry.ready?Recording{reinterpret_cast<Id>(native),entry.lifetime,entry.reset,entry.device}:Recording{};
    return {};
}
bool HasObservedList(ID3D12GraphicsCommandList* native){for(const auto& entry:observed_lists)if(entry.native==native)return true;return false;}
void ForgetObservedList(Id address){for(auto& entry:observed_lists)if(reinterpret_cast<Id>(entry.native)==address)entry={};}
void AdvanceObservedList(ID3D12GraphicsCommandList* native){
    for(auto& entry:observed_lists)if(entry.native==native){if(entry.reset!=UINT64_MAX)++entry.reset;else entry.ready=false;break;}
}
bool EnsureSourceObservation(reshade::api::command_list* owner,bool init){
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_acquire)||!owner||!owner->get_device()||
       owner->get_device()->get_api()!=reshade::api::device_api::d3d12)return false;
    auto* native=reinterpret_cast<ID3D12GraphicsCommandList*>(owner->get_native());if(!native)return false;
    const auto now=GetTickCount64();
    if(!init){Lock lock;for(const auto& entry:observed_lists)if(entry.native==native){
        if(entry.ready)return true;if(entry.installing||now<entry.retry_after)return false;break;}}
    if(ID3D12GraphicsCommandList_GetType(native)!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
    ObservedSourceList* selected=nullptr;Id lifetime=0,device=0;
    {
        Lock lock;lifetime=ObjectId(reinterpret_cast<ID3D12Object*>(native));device=DeviceOf(reinterpret_cast<ID3D12DeviceChild*>(native));
        if(!lifetime||!device)return false;
        for(auto& entry:observed_lists){if(entry.native==native){selected=&entry;break;}
            if(!entry.native&&!selected)selected=&entry;}
        if(!selected)return false;
        if(selected->native&&(selected->lifetime!=lifetime||selected->device!=device))*selected={};
        if(selected->ready)return true;if(selected->installing||now<selected->retry_after)return false;
        if(!selected->native){selected->native=native;selected->lifetime=lifetime;selected->device=device;}
        selected->installing=true;selected->retry_after=now+1000;
    }
    // MinHook publication may suspend other threads; never hold source_lock.
    const auto coverage=EnsurePostBoundaryNativeHooks(native);
    const bool reset=EnsureNativeCommandListResetHookForNative(native);
    const auto required=nr_post_boundary::Legacy|nr_post_boundary::Operations;
    const bool ready=reset&&(coverage&required)==required&&
        !(coverage&(nr_post_boundary::HookMissing|nr_post_boundary::InterfaceUnknown|nr_post_boundary::IdentityUnknown));
    {Lock lock;if(selected->native==native&&selected->lifetime==lifetime){selected->installing=false;selected->ready=ready;}}
    (ready?source_observer_ready:source_observer_refused).fetch_add(1,std::memory_order_relaxed);return ready;
}
void OnSourceInitList(reshade::api::command_list* owner){(void)EnsureSourceObservation(owner,true);}
void OnSourceResetList(reshade::api::command_list* owner){(void)EnsureSourceObservation(owner,false);}
bool SourceGuidesAwaitSubmission();
bool MayObserveSubmission(){return SourceGuidesAwaitSubmission()||
    (nr_processing_runtime::work_enabled.load(std::memory_order_acquire)&&source_receipts_pending.load(std::memory_order_acquire));}
