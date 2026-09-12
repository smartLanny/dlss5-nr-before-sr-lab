// Admission and mutation run under g_chain_cs. A privately detached, fully
// retired context may destroy its pool outside the lock, like its textures.
// Each lease uses RecordNrUseLocked; no second submission ledger is introduced.
bool IsSameRrDepthDevice(ID3D12Device*,ID3D12Device*);
void DropBindingPacket(NrBindingPacket& packet) {
    for(auto& heap:packet.heaps)ReleaseCom(heap);
    for(auto& set:packet.plan.heaps)for(unsigned i=0;i<set.count;++i)
        ReleaseCom(set.views[i].resource);
    for(auto& resource:packet.plan.retained)ReleaseCom(resource);
    packet={};
}
bool CanDestroyBindingsLocked(const NrContext& context) {
    if(!context.binding_pool)return true;
    for(const auto& packet:context.binding_pool->packets)
        if(packet.valid&&!CanMutateSlotLocked(packet.lease))return false;
    return true;
}
void DestroyBindings(NrContext& context) {
    if(!context.binding_pool)return;
    for(auto& packet:context.binding_pool->packets)DropBindingPacket(packet);
    delete context.binding_pool;context.binding_pool=nullptr;
}
// Owned planes must not become invisible to the allocation ledger while a
// cached table still holds them. Called only for actual replacement/rebinding.
bool InvalidateSlotBindingsLocked(const NrSlot& slot) {
    const auto references=[&](const NrBindingPacket& p){return p.valid&&
        (p.plan.primary_owner==&slot||p.plan.secondary_owner==&slot);};
    for(const auto& context:g_contexts)if(context.binding_pool)
        for(const auto& packet:context.binding_pool->packets)
            if(references(packet)&&!CanMutateSlotLocked(packet.lease))return false;
    for(auto& context:g_contexts)if(context.binding_pool)
        for(auto& packet:context.binding_pool->packets)
            if(references(packet))DropBindingPacket(packet);
    return true;
}
void WriteDescriptorPlan(ID3D12Device* device,ID3D12DescriptorHeap* heap,
                         const nr_binding::DescriptorSet& set) {
    auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    const auto stride=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for(unsigned i=0;i<set.count;++i,cpu.ptr+=stride) {
        const auto& view=set.views[i];
        if(view.kind==nr_binding::ViewKind::Srv2D) {
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=view.format;
            d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
            ID3D12Device_CreateShaderResourceView(device,view.resource,&d,cpu);
        } else if(view.kind==nr_binding::ViewKind::Uav2D||view.kind==nr_binding::ViewKind::RawUav) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=view.format;
            d.ViewDimension=view.kind==nr_binding::ViewKind::RawUav?D3D12_UAV_DIMENSION_BUFFER:D3D12_UAV_DIMENSION_TEXTURE2D;
            if(view.kind==nr_binding::ViewKind::RawUav){d.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;d.Buffer.NumElements=view.elements;}
            ID3D12Device_CreateUnorderedAccessView(device,view.resource,nullptr,&d,cpu);
        }
    }
}
bool BindingQueueAdmitted(ID3D12GraphicsCommandList* list,const NrContext& context,
                          const NrBindingPacket& packet) {
    if(!CanBeginContextUseLocked(list,context,false)||packet.lease.awaiting_fence_signal)return false;
    if(IsSlotFenceCompleteLocked(packet.lease))return true;
    CommandListQueueMarker marker{};
    return ReadLastSubmissionQueueMarker(list,marker)&&marker.stable_submissions>=kRequiredStableQueueSubmissions&&
        !(marker.flags&kQueueMarkerMigrated)&&packet.lease.last_submission_queue&&
        packet.lease.last_submission_queue->identity==marker.identity;
}
NrBindingPacket* AcquireBindingPacket(ID3D12Device* device,ID3D12GraphicsCommandList* list,
    NrContext& context,const nr_binding::Plan& plan,nr_binding::Refusal* result=nullptr) {
    if(result)*result=nr_binding::Refusal::None;
    const auto reject=[&](nr_binding::Refusal reason)->NrBindingPacket*{
        if(result)*result=reason;return nullptr;
    };
    if(!device||!plan.Valid())return reject(nr_binding::Refusal::InvalidPlan);
    if(!IsSameRrDepthDevice(device,g_codec.device))return reject(nr_binding::Refusal::DeviceIdentity);
    if(!CanBeginContextUseLocked(list,context,false))return reject(nr_binding::Refusal::Queue);
    if(!context.binding_pool)context.binding_pool=new(std::nothrow)NrBindingPool;
    if(!context.binding_pool)return reject(nr_binding::Refusal::PoolAllocation);
    auto& pool=*context.binding_pool;
    for(auto& packet:pool.packets)if(packet.valid&&packet.plan==plan&&BindingQueueAdmitted(list,context,packet)) {
        if(!RecordNrUseLocked(list,context,packet.lease))return reject(nr_binding::Refusal::Recording);
        packet.last_used=++pool.serial;return &packet;
    }
    // Verify every newly admitted resource against the actual execution
    // device. ReShade wrappers are resolved by the same Core identity proof.
    ID3D12Resource* checked[nr_binding::kDescriptors*nr_binding::kHeaps+2]{};
    unsigned checked_count=0;
    for(const auto& set:plan.heaps)for(unsigned i=0;i<set.count;++i) {
        auto* resource=set.views[i].resource;if(!resource)continue;
        bool duplicate=false;for(unsigned n=0;n<checked_count;++n)duplicate|=checked[n]==resource;
        if(duplicate)continue;
        ID3D12Device* owner=nullptr;
        const auto hr=ID3D12Resource_GetDevice(resource,IID_ID3D12Device,reinterpret_cast<void**>(&owner));
        const bool same=SUCCEEDED(hr)&&IsSameRrDepthDevice(device,owner);ReleaseCom(owner);
        if(!same)return reject(nr_binding::Refusal::DeviceIdentity);
        checked[checked_count++]=resource;
    }
    for(auto* resource:plan.retained)if(resource) {
        ID3D12Device* owner=nullptr;
        const auto hr=ID3D12Resource_GetDevice(resource,IID_ID3D12Device,reinterpret_cast<void**>(&owner));
        const bool same=SUCCEEDED(hr)&&IsSameRrDepthDevice(device,owner);ReleaseCom(owner);
        if(!same)return reject(nr_binding::Refusal::DeviceIdentity);
    }
    NrBindingPacket* free=nullptr;
    for(auto& packet:pool.packets)if(CanMutateSlotLocked(packet.lease)&&
        (!free||(!packet.valid&&free->valid)||packet.last_used<free->last_used))free=&packet;
    if(!free)return reject(nr_binding::Refusal::InFlightCapacity);
    DropBindingPacket(*free);
    // Construct privately. A failed heap allocation has no recorded users.
    for(unsigned i=0;i<nr_binding::kHeaps;++i)if(plan.heaps[i].count) {
        D3D12_DESCRIPTOR_HEAP_DESC d{};d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        d.NumDescriptors=plan.heaps[i].count;d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(FAILED(ID3D12Device_CreateDescriptorHeap(device,&d,IID_ID3D12DescriptorHeap,
            reinterpret_cast<void**>(&free->heaps[i])))){DropBindingPacket(*free);return reject(nr_binding::Refusal::HeapAllocation);}
    }
    free->plan=plan;
    for(auto* resource:free->plan.retained)if(resource)ID3D12Resource_AddRef(resource);
    for(auto& set:free->plan.heaps)for(unsigned i=0;i<set.count;++i)
        if(set.views[i].resource)ID3D12Resource_AddRef(set.views[i].resource);
    for(unsigned i=0;i<nr_binding::kHeaps;++i)if(free->heaps[i])WriteDescriptorPlan(device,free->heaps[i],free->plan.heaps[i]);
    free->valid=true;
    if(!RecordNrUseLocked(list,context,free->lease)){DropBindingPacket(*free);return reject(nr_binding::Refusal::Recording);}
    free->last_used=++pool.serial;return free;
}
