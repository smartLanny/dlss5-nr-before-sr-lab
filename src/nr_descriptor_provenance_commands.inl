// Command identity uses the exact forwarded lifetime token. No descriptor GPU
// memory, virtual-address bit pattern, or shader-table inference is involved.
bool NrDescriptorObserveCommandLayer(ID3D12GraphicsCommandList *list) {
    if(!g_nr_descriptor_active_call && !PostBoundaryRecordingActive())return false;
    const auto identity=SnapshotPostBoundaryList(list);
    if(!identity.known && PostBoundaryRecordingActive())
        MarkPostBoundaryObservationGap(nr_post_boundary::IdentityUnknown);
    return identity.known && identity.address==reinterpret_cast<std::uint64_t>(list);
}
std::uint64_t NrDescriptorBoundHeap(ID3D12GraphicsCommandList *list) {
    const auto identity=SnapshotPostBoundaryList(list);
    if(!identity.known)return 0;
    std::uint64_t address=0;
    AcquireSRWLockShared(&g_command_state_lock);
    for(const auto &entry:g_command_states)if(entry.token==identity.token && entry.native_resets==identity.reset &&
            reinterpret_cast<std::uint64_t>(entry.native)==identity.address && entry.lifetime_ready) {
        if(entry.state && entry.state->known &&
           !(entry.state->unknown_domains&D3D12_GRAPHICS_STATE_DESCRIPTOR_HEAP))
            address=reinterpret_cast<std::uint64_t>(entry.state->heaps[0]);
        break;
    }
    ReleaseSRWLockShared(&g_command_state_lock);
    return address?g_nr_descriptors.HeapAt(address).id:0;
}
nr_descriptor::Pair NrDescriptorCaptureUav(ID3D12GraphicsCommandList *list,D3D12_GPU_DESCRIPTOR_HANDLE gpu,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu,ID3D12Resource *resource) {
    if(!g_nr_descriptor_active_call && !PostBoundaryRecordingActive())return {};
    const auto target=g_nr_descriptors.ResourceAt(reinterpret_cast<std::uint64_t>(resource));
    return g_nr_descriptors.UavPair(target.device,target,NrDescriptorBoundHeap(list),gpu.ptr,cpu.ptr);
}
nr_descriptor::Receipt NrDescriptorCaptureRtv(D3D12_CPU_DESCRIPTOR_HANDLE cpu) {
    return PostBoundaryRecordingActive()?g_nr_descriptors.Cpu(cpu.ptr,2,nr_descriptor::Kind::Rtv):nr_descriptor::Receipt{};
}
nr_descriptor::RtvBatch NrDescriptorCaptureRtvBatch(UINT count,const D3D12_CPU_DESCRIPTOR_HANDLE *views,BOOL contiguous) {
    nr_descriptor::RtvBatch batch;
    if(!PostBoundaryRecordingActive() || !count)return batch;
    if(count>8 || !views){batch.unknown=true;return batch;}
    batch.count=count;
    for(unsigned i=0;i<count;++i) {
        const auto handle=contiguous?g_nr_descriptors.OffsetCpu(views[0].ptr,2,i):views[i].ptr;
        batch.views[i]=g_nr_descriptors.Cpu(handle,2,nr_descriptor::Kind::Rtv);
    }
    return batch;
}
void NrDescriptorFillEvent(nr_post_boundary::Event &event,const nr_descriptor::Receipt &view,
        UINT count,const D3D12_RECT *rects) {
    if(!view)return;
    event.descriptor_resource=view.view.resource.id;event.descriptor_revision=view.stamp.revision;
    event.descriptor_format=view.view.format;
    event.subresources[0]=view.view.mip;event.subresources[1]=view.view.plane;
    if(event.kind!=nr_post_boundary::Kind::ClearRtv && event.kind!=nr_post_boundary::Kind::ClearUav)return;
    const auto range=nr_descriptor::DescribeClear(view.view,count,rects);
    event.clear_kind=static_cast<unsigned>(range.kind);event.clear_rect_count=range.count;
    std::memcpy(event.clear_bounds,range.bounds,sizeof(event.clear_bounds));
}
void ObservePostBoundaryRtvDescriptor(ID3D12GraphicsCommandList *list,nr_post_boundary::Kind kind,
        const nr_descriptor::Receipt &view,UINT count=0,const D3D12_RECT *rects=nullptr) {
    if(!PostBoundaryRecordingActive())return;
    const auto target=PostBoundaryTarget();
    const auto target_identity=g_native_output_trace?g_native_output_trace->descriptor_resource:
        g_post_boundary_descriptor_resource.load(std::memory_order_acquire);
    const bool current=view && g_nr_descriptors.Current(view);
    if(current && view.view.resource.address!=target) {
        ObservePostBoundaryOtherRtv(SnapshotPostBoundaryList(list),kind);
        return; // Known other targets have separate bounded counts, never Output events.
    }
    auto event=PostBoundaryEvent(list,kind);
    const bool exact=current && target_identity && view.view.resource.id==target_identity;
    if(current)NrDescriptorFillEvent(event,view,count,rects);
    event.writes=exact && kind==nr_post_boundary::Kind::ClearRtv;
    ObservePostBoundaryEvent(event,exact?target:0);
}
void ObservePostBoundaryRtvBatch(ID3D12GraphicsCommandList *list,const nr_descriptor::RtvBatch &batch) {
    if(batch.unknown)ObservePostBoundaryRtvDescriptor(list,nr_post_boundary::Kind::BindTargets,{});
    for(unsigned i=0;i<batch.count;++i)
        ObservePostBoundaryRtvDescriptor(list,nr_post_boundary::Kind::BindTargets,batch.views[i]);
}
void ObservePostBoundaryUavDescriptor(ID3D12GraphicsCommandList *list,ID3D12Resource *resource,
        const nr_descriptor::Pair &pair,UINT count,const D3D12_RECT *rects) {
    NrDescriptorObserveNativeClear(list,resource,pair);
    if(!PostBoundaryRecordingActive())return;
    const auto target=PostBoundaryTarget();
    if(resource && reinterpret_cast<std::uint64_t>(resource)!=target)return;
    auto event=PostBoundaryEvent(list,nr_post_boundary::Kind::ClearUav);
    event.writes=resource!=nullptr;
    if(pair && g_nr_descriptors.Current(pair))NrDescriptorFillEvent(event,pair.cpu,count,rects);
    ObservePostBoundaryEvent(event,resource?target:0);
}
