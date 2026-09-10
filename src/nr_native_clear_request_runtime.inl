// No lock crosses the original Clear or native Evaluate. TLS prevents nested
// and concurrent calls from teaching another invocation's Output contract.
nr_native_clear::Ledger<> g_nr_native_clear_ledger;
nr_native_clear::Receipt NrNativeClearCandidate(const nr_native_format::Key& key,unsigned config) {
    AcquireSRWLockShared(&g_nr_format_lock);
    const auto result=g_nr_native_clear_ledger.Find(key,config);
    ReleaseSRWLockShared(&g_nr_format_lock);
    return result;
}
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
struct NrNativeClearScope;
thread_local NrNativeClearScope* g_nr_native_clear_scope=nullptr;
struct NrNativeClearScope {
    NrNativeClearScope* previous=nullptr;
    nr_native_clear::Receipt request;
    bool active=true;
    NrNativeClearScope(const NrNativeFormatScope& format,ID3D12GraphicsCommandList* list) {
        previous=g_nr_native_clear_scope;g_nr_native_clear_scope=this;
        request.key=format.keys[1];request.call=format.call;
        request.config=g_config_generation.load(std::memory_order_acquire);
        request.resource_address=reinterpret_cast<std::uint64_t>(format.resources[1]);
        const auto identity=SnapshotPostBoundaryList(list);
        const auto output=g_nr_descriptors.ResourceAt(request.resource_address);
        request.list=identity.token;request.reset=identity.reset;request.list_address=identity.address;
        request.resource=output.id;
        request.identity=format.active && identity.known && output && output.device==request.key.device &&
            NrDescriptorDeviceCoverage(format.device).ready &&
            NrDescriptorCommandHooksReady(reinterpret_cast<ID3D12GraphicsCommandList*>(identity.address));
        if(format.resources[1]) {
            const auto desc=ID3D12Resource_GetDesc(format.resources[1]);
            request.shape=desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.Width &&
                desc.Width<=UINT_MAX && desc.Height && desc.DepthOrArraySize==1 &&
                desc.MipLevels==1 && desc.SampleDesc.Count==1;
            request.width=static_cast<unsigned>(desc.Width);request.height=desc.Height;
        }
    }
    nr_native_clear::Invocation Finish(bool success) {
        if(active) {
            g_nr_native_clear_scope=previous;active=false;
            request.finished=true;request.success=success;
            AcquireSRWLockExclusive(&g_nr_format_lock);
            g_nr_native_clear_ledger.Commit(request);
            ReleaseSRWLockExclusive(&g_nr_format_lock);
        }
        return {request.call,request};
    }
    ~NrNativeClearScope(){Finish(false);}
};
void NrObserveNativeClearRequest(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,
        const nr_descriptor::Pair& before,const nr_descriptor::Pair& after,UINT count,const D3D12_RECT* rects) {
    auto* scope=g_nr_native_clear_scope;
    if(!scope || !scope->active)return;
    auto& r=scope->request;
    if(reinterpret_cast<std::uint64_t>(resource)!=r.resource_address)return;
    const auto identity=SnapshotPostBoundaryList(list);
    const auto output=g_nr_descriptors.ResourceAt(r.resource_address);
    if(!identity.known || identity.token!=r.list || identity.reset!=r.reset || identity.address!=r.list_address ||
       !output || output.id!=r.resource || output.device!=r.key.device) {r.unknown=true;return;}
    (void)count;(void)rects; // Rect coverage is diagnostic; the typed view is unchanged.
    r.Observe(before,after);
}
bool NrNativeClearMayFinish(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,
        unsigned long long generation,unsigned format,const nr_native_clear::Receipt& prepared,
        const nr_native_clear::Invocation& current) {
    ID3D12Device* device=nullptr;
    if(!list || FAILED(ID3D12GraphicsCommandList_GetDevice(list,IID_ID3D12Device,
            reinterpret_cast<void**>(&device))) || !device)return false;
    const auto key=NrFormatKey(device,resource,generation,true);
    ID3D12Device_Release(device);
    const auto identity=SnapshotPostBoundaryList(list);
    const auto target=g_nr_descriptors.ResourceAt(reinterpret_cast<std::uint64_t>(resource));
    const auto synchronous=NrFormatEvidence(key);
    return !synchronous.failed && !synchronous.view.conflict &&
        (!synchronous.Resolved() || synchronous.Resolved()==format) &&
        identity.known && target && target.id==current.request.resource && target.device==key.device &&
        nr_native_clear::Admit(prepared,current,key,g_config_generation.load(std::memory_order_acquire),
            identity.token,identity.reset,identity.address,format);
}
#endif
