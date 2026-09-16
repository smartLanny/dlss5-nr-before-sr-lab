// Same-recording, owned guide copies. No CPU waits, borrowed texture history,
// guessed states, or copies on an unrelated presentation queue.
#include "nr_native_guide_snapshot.inl"
#include "nr_source_guide_diagnostics.inl"
struct GuideCopy {
    Source source{};Recording recording{};NrGuideBinding guides{};
    NrGuideSignature source_signature{};
    ID3D12Resource* inputs[2]{};ID3D12Resource* owned[2]{};
    ID3D12Fence *producer_fence=nullptr,*consumer_fence=nullptr;
    Id producer_value=0,consumer_value=0,queue=0,bytes=0,opportunity=0;
    unsigned output_width=0,output_height=0,config_generation=0;
    unsigned owned_state[2]{};
    NativeGuideSnapshotObjects snapshot{};
    bool shader_snapshot=false;
    bool reserved=false,recorded=false,native_result_known=false,native_succeeded=false;
    bool submitted=false,acquired=false,consumed=false,inline_processed=false,fg_consumed=false,retired=false,poisoned=false;
};
GuideCopy guide_copies[8]{};
struct ReceiptStream {Id stream=0,epoch=0,device=0,latest_sequence=0,latest_opportunity=0;};
ReceiptStream receipt_streams[64]{};
struct DeviceOpportunity {Id device=0,latest=0;};
DeviceOpportunity receipt_devices[16]{};
Id next_receipt_opportunity=1;
std::atomic<bool> guides_awaiting_submission{false};
std::atomic<bool> guides_awaiting_retirement{false};
thread_local GuideCopy* present_guides=nullptr;
thread_local bool present_blocked_by_fg=false;
constexpr Id guide_budget=256ull*1024*1024;
SRWLOCK guide_allocation_lock=SRWLOCK_INIT;
bool FenceDone(ID3D12Fence* fence,Id value) {
    if(!fence||!value)return false;const auto done=ID3D12Fence_GetCompletedValue(fence);
    return done!=UINT64_MAX&&done>=value;
}
bool GuideReusable(GuideCopy& c) {
    if(c.poisoned||c.reserved||c.acquired)return false;
    if(!c.recorded)return true;
    return c.submitted&&FenceDone(c.producer_fence,c.producer_value)&&
        (!c.consumer_fence||FenceDone(c.consumer_fence,c.consumer_value));
}
ReceiptStream* ReceiptStreamFor(Source source,bool create) {
    ReceiptStream* free=nullptr;
    for(auto& stream:receipt_streams){
        if(stream.stream==source.stream&&stream.epoch==source.epoch)return &stream;
        if(!stream.stream&&!free)free=&stream;
    }
    if(create&&free)*free={source.stream,source.epoch,0,0,0};
    return create?free:nullptr;
}
DeviceOpportunity* ReceiptDeviceFor(Id device,bool create) {
    DeviceOpportunity* free=nullptr;
    for(auto& item:receipt_devices){if(item.device==device)return &item;if(!item.device&&!free)free=&item;}
    if(create&&free)*free={device,0};return create?free:nullptr;
}
void BeginNativeReceiptOpportunityLocked(Source source,Id device) {
    if(!source||!device||next_receipt_opportunity==UINT64_MAX)return;
    auto* stream=ReceiptStreamFor(source,true);auto* owner=ReceiptDeviceFor(device,true);
    if(!stream||!owner||stream->latest_sequence>=source.sequence)return;
    const auto opportunity=next_receipt_opportunity++;
    stream->device=device;stream->latest_sequence=source.sequence;stream->latest_opportunity=opportunity;
    owner->latest=opportunity;
}
void BeginNativeReceiptOpportunity(Source source,Id device) {Lock lock;BeginNativeReceiptOpportunityLocked(source,device);}
void CompleteNativeReceiptLocked(Source source,bool succeeded) {
    for(auto& c:guide_copies)if(c.source==source&&c.recorded&&!c.native_result_known){
        c.native_result_known=true;c.native_succeeded=succeeded;
        if(succeeded)++guide_receipt_diagnostics.native_success;else ++guide_receipt_diagnostics.native_failure;
    }
}
void CompleteNativeReceipt(Source source,bool succeeded) {Lock lock;CompleteNativeReceiptLocked(source,succeeded);}
void MarkReceiptInlineProcessedLocked(Source source) {
    for(auto& c:guide_copies)if(c.source==source)c.inline_processed=true;
}
void MarkReceiptFgConsumedLocked(Source source) {
    for(auto& c:guide_copies)if(c.source==source)c.fg_consumed=true;
}
void RetireReceiptStreamLocked(Id stream,Id epoch) {
    for(auto& item:receipt_streams)if(item.stream==stream&&item.epoch==epoch)item={};
    for(auto& c:guide_copies)if(c.source.stream==stream&&c.source.epoch==epoch)c.retired=true;
}
void CollectGuideInputs() {
    if(!guides_awaiting_retirement.load(std::memory_order_acquire))return;
    ID3D12Resource* released[16]{};unsigned count=0;bool pending=false;
    {
        Lock lock;
        for(auto& c:guide_copies)if(!c.reserved&&(c.inputs[0]||c.inputs[1])){
            if(c.submitted&&FenceDone(c.producer_fence,c.producer_value)){
                for(auto& p:c.inputs)if(p){released[count++]=p;p=nullptr;}
            }else pending=true;
        }
        guides_awaiting_retirement.store(pending,std::memory_order_release);
    }
    // Original app textures only support the producer copy. Their lifetime
    // does not depend on how long Present keeps the owned guide snapshot.
    for(unsigned i=0;i<count;++i)ReleaseCom(released[i]);
}
void ClearGuideCopy(GuideCopy& c) {
    for(auto& p:c.inputs)ReleaseCom(p);for(auto& p:c.owned)ReleaseCom(p);
    ReleaseNativeGuideSnapshot(c.snapshot);
    ReleaseCom(c.producer_fence);ReleaseCom(c.consumer_fence);c={};
}
bool MakeGuideCopy(ID3D12Device* device,ID3D12Resource* input,unsigned width,unsigned height,
    ID3D12Resource*& output,Id& bytes) {
    auto desc=ID3D12Resource_GetDesc(input);
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.DepthOrArraySize!=1||
       desc.MipLevels!=1||desc.SampleDesc.Count!=1||!width||!height||width>16384||height>16384)return false;
    desc.Width=width;desc.Height=height;
    desc.Flags=static_cast<D3D12_RESOURCE_FLAGS>(static_cast<unsigned>(desc.Flags)&~static_cast<unsigned>(D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE));
    const auto allocation=ID3D12Device_GetResourceAllocationInfo(device,0,1,&desc);
    if(!allocation.SizeInBytes||allocation.SizeInBytes==UINT64_MAX||allocation.SizeInBytes>guide_budget-bytes)return false;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    if(FAILED(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_COMMON,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&output))))return false;
    bytes+=allocation.SizeInBytes;return true;
}
bool CaptureGuides(ID3D12GraphicsCommandList* list,Source source,const NrGuideBinding& input,
    unsigned output_width,unsigned output_height,unsigned config_generation,bool native_read_boundary=false) {
    GuideCaptureAttempt diagnostic(source);GuideCaptureStage("capture-disabled-or-missing-input");
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_acquire)||!source||!list||!input.depth||!input.motion||
       input.signature.linear_depth||!output_width||!output_height||output_width>16384||output_height>16384)return false;
    const auto depth_desc=ID3D12Resource_GetDesc(input.depth);
    const auto motion_desc=ID3D12Resource_GetDesc(input.motion);
    const auto& sig=input.signature;
    if(active_guide_capture){auto& d=*active_guide_capture;d.config=config_generation;
        d.depth_physical_width=static_cast<unsigned>(depth_desc.Width);d.depth_physical_height=depth_desc.Height;
        d.depth_x=sig.depth_x;d.depth_y=sig.depth_y;d.depth_width=sig.depth_width;d.depth_height=sig.depth_height;
        d.motion_width=sig.motion_source_width;d.motion_height=sig.motion_source_height;
        d.output_width=output_width;d.output_height=output_height;}
    if(!NativeGuideRectFits(depth_desc,sig.depth_x,sig.depth_y,sig.depth_width,sig.depth_height)||
       !NativeGuideRectFits(motion_desc,sig.motion_x,sig.motion_y,sig.motion_source_width,sig.motion_source_height))
        return GuideCaptureDecline("invalid-guide-subrect");
    if(native_read_boundary&&(!input.valid_motion_scale_x||!input.valid_motion_scale_y))
        return GuideCaptureDecline("native-motion-scale-unverified");
    const bool full_depth=(depth_desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)!=0;
    GuideCopy* selected=nullptr;unsigned before[2]{};bool known[2]{};bool shader_snapshot=false;Recording recording{};
    // Lazily created only on the shader fallback. Destruction restores the
    // exact native bindings; copy-only capture does not pay for this envelope.
    std::unique_ptr<NativeCommandStateScope> native_state;
    struct DiscardUnmodifiedCapture {
        std::unique_ptr<NativeCommandStateScope>& state;bool modified=false;
        void MarkModified(){modified=true;}
        ~DiscardUnmodifiedCapture(){if(state&&!modified)state->active=false;}
    } discard_unmodified{native_state};
    {
        Lock lock;recording=RecordingOf(list);
        if(!recording)return GuideCaptureDecline("recording-identity-unavailable");
        known[0]=StateOf(recording,ResourceOf(input.depth),before[0]);
        known[1]=StateOf(recording,ResourceOf(input.motion),before[1]);
    }
    if(active_guide_capture){active_guide_capture->depth_state_observed=known[0];active_guide_capture->motion_state_observed=known[1];}
    if(!known[0]||!known[1]) {
        if(!native_read_boundary)return GuideCaptureDecline("guide-state-unobserved-outside-NGX");
        // NGX's input seam is shader-readable, not necessarily one particular
        // legacy StateBefore. Never invent a barrier from an unobserved state.
        // A contradictory observed state is NOT converted into missing evidence.
        constexpr unsigned read=static_cast<unsigned>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        constexpr unsigned other_read=static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)|
            static_cast<unsigned>(D3D12_RESOURCE_STATE_DEPTH_READ)|static_cast<unsigned>(D3D12_RESOURCE_STATE_COPY_SOURCE);
        for(unsigned i=0;i<2;++i)if(known[i]&&(!(before[i]&read)||(before[i]&~(read|other_read))))
            return GuideCaptureDecline("native-guide-read-state-conflict");
        native_state.reset(new(std::nothrow) NativeCommandStateScope(list));
        if(!native_state||!native_state->active)return GuideCaptureDecline("guide-command-state-unavailable");
        list=native_state->command_list;shader_snapshot=true;
        if(active_guide_capture)active_guide_capture->shader_snapshot=true;
    }
    const unsigned depth_copy_width=shader_snapshot||!full_depth?sig.depth_width:static_cast<unsigned>(depth_desc.Width);
    const unsigned depth_copy_height=shader_snapshot||!full_depth?sig.depth_height:depth_desc.Height;
    const DXGI_FORMAT depth_copy_format=shader_snapshot?DXGI_FORMAT_R32_FLOAT:depth_desc.Format;
    const DXGI_FORMAT motion_copy_format=shader_snapshot?NativeGuideMotionCopyFormat(motion_desc.Format):motion_desc.Format;
    {
        Lock lock;recording=RecordingOf(list);if(!recording)return GuideCaptureDecline("recording-identity-unavailable");
        const auto* stream=ReceiptStreamFor(source,false);
        if(!stream||stream->device!=recording.device||stream->latest_sequence!=source.sequence||!stream->latest_opportunity)
            return GuideCaptureDecline("source-opportunity-or-device-mismatch");
        for(auto& copy:guide_copies)if(copy.source==source&&copy.recorded&&!copy.poisoned){GuideCaptureStage("already-recorded");return true;}
        // Prefer genuinely empty or consumed slots to a fresh unconsumed
        // receipt. Do not overwrite the last ready frame with an unsubmitted one.
        for(auto& copy:guide_copies)if(GuideReusable(copy)&&(!copy.recorded||copy.consumed||copy.retired||copy.inline_processed||copy.fg_consumed)){selected=&copy;break;}
        if(!selected)for(auto& copy:guide_copies)if(GuideReusable(copy)){
            const auto* old=ReceiptStreamFor(copy.source,false);
            if(!old||(old->latest_sequence>copy.source.sequence&&old->latest_sequence-copy.source.sequence>1)){selected=&copy;break;}
        }
        if(!selected)return GuideCaptureDecline("guide-slots-busy");
        selected->reserved=true;
    }
    // Texture allocations and all command recording happen without source_lock:
    // the real Core's native copy/barrier observers may synchronously reenter.
    // Transfer ownership into a private draft while the public slot remains
    // reserved. Reentrant resource destruction never occurs under source_lock.
    GuideCopy c;{Lock lock;c=*selected;}ID3D12Device* device=nullptr;
    AcquireSRWLockExclusive(&guide_allocation_lock);
    bool made=SUCCEEDED(ID3D12GraphicsCommandList_GetDevice(list,IID_ID3D12Device,reinterpret_cast<void**>(&device)))&&device;
    if(made){ID3D12Device* depth_device=nullptr;ID3D12Device* motion_device=nullptr;
        const bool same=SUCCEEDED(ID3D12Resource_GetDevice(input.depth,IID_ID3D12Device,reinterpret_cast<void**>(&depth_device)))&&
            SUCCEEDED(ID3D12Resource_GetDevice(input.motion,IID_ID3D12Device,reinterpret_cast<void**>(&motion_device)))&&
            IsSameRrDepthDevice(device,depth_device)&&IsSameRrDepthDevice(device,motion_device);
        ReleaseCom(depth_device);ReleaseCom(motion_device);made=same;}
    if(made&&shader_snapshot)made=NativeGuideViewSupported(device,NativeGuideDepthView(depth_desc.Format),false)&&
        NativeGuideViewSupported(device,NativeGuideMotionView(motion_desc.Format),false)&&
        NativeGuideViewSupported(device,depth_copy_format,true)&&NativeGuideViewSupported(device,motion_copy_format,true);
    GuideCaptureStage(made?"guide-allocation-or-pipeline":"guide-device-or-view-support");
    bool reuse=made&&c.owned[0]&&c.owned[1]&&c.producer_fence&&c.shader_snapshot==shader_snapshot;
    if(reuse){const auto d=ID3D12Resource_GetDesc(c.owned[0]),m=ID3D12Resource_GetDesc(c.owned[1]);
        ID3D12Device* owner=nullptr;
        reuse=SUCCEEDED(ID3D12Resource_GetDevice(c.owned[0],IID_ID3D12Device,reinterpret_cast<void**>(&owner)))&&
            IsSameRrDepthDevice(device,owner)&&d.Width==depth_copy_width&&d.Height==depth_copy_height&&d.Format==depth_copy_format&&
            m.Width==sig.motion_source_width&&m.Height==sig.motion_source_height&&m.Format==motion_copy_format;
        ReleaseCom(owner);}
    if(!reuse){ClearGuideCopy(c);c.reserved=true;
        Id all_bytes=0;{Lock lock;for(const auto& copy:guide_copies)if(&copy!=selected)all_bytes+=copy.bytes;}
        c.bytes=all_bytes;
        if(made){
            if(shader_snapshot)made=MakeNativeGuidePlane(device,depth_copy_width,depth_copy_height,depth_copy_format,c.owned[0],c.bytes,guide_budget)&&
                MakeNativeGuidePlane(device,sig.motion_source_width,sig.motion_source_height,motion_copy_format,c.owned[1],c.bytes,guide_budget);
            else made=MakeGuideCopy(device,input.depth,depth_copy_width,depth_copy_height,c.owned[0],c.bytes)&&
                MakeGuideCopy(device,input.motion,sig.motion_source_width,sig.motion_source_height,c.owned[1],c.bytes);
            if(made)made=SUCCEEDED(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(&c.producer_fence)));
        }
        c.bytes-=all_bytes;
    }else {for(auto& resource:c.inputs)ReleaseCom(resource);ReleaseCom(c.consumer_fence);c.consumer_value=0;}
    if(made&&shader_snapshot){made=BuildNativeGuideSnapshot(device,c.snapshot);
        if(made)BindNativeGuideSnapshot(device,c.snapshot,input,c.owned[0],c.owned[1]);}
    ReleaseCom(device);
    if(!made){ClearGuideCopy(c);{Lock lock;*selected={};}ReleaseSRWLockExclusive(&guide_allocation_lock);return false;}
    c.shader_snapshot=shader_snapshot;
    c.source=source;c.recording=recording;c.output_width=output_width;c.output_height=output_height;c.config_generation=config_generation;
    {Lock lock;const auto* stream=ReceiptStreamFor(source,false);c.opportunity=stream?stream->latest_opportunity:0;}
    c.source_signature=input.signature;c.recorded=c.native_result_known=c.native_succeeded=c.submitted=false;
    c.acquired=c.consumed=c.inline_processed=c.fg_consumed=c.retired=c.poisoned=false;c.queue=0;
    {Lock lock;selected->bytes=c.bytes;}
    ReleaseSRWLockExclusive(&guide_allocation_lock);
    c.inputs[0]=input.depth;c.inputs[1]=input.motion;
    for(auto* resource:c.inputs)ID3D12Resource_AddRef(resource);
    c.guides=input;c.guides.depth=c.owned[0];c.guides.motion=c.owned[1];
    auto& s=c.guides.signature;
    const unsigned widths[]={s.depth_width,s.motion_source_width},heights[]={s.depth_height,s.motion_source_height};
    const unsigned xs[]={s.depth_x,s.motion_x},ys[]={s.depth_y,s.motion_y};
    GuideCaptureStage("recording-guide-copy");
    if(shader_snapshot){discard_unmodified.MarkModified();RecordNativeGuideSnapshot(list,c.snapshot,input,c.owned[0],c.owned[1],c.owned_state);}
    else for(unsigned i=0;i<2;++i){
        auto input_state=static_cast<D3D12_RESOURCE_STATES>(before[i]);auto output_state=static_cast<D3D12_RESOURCE_STATES>(c.owned_state[i]);
        Transition(list,c.inputs[i],input_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(list,c.owned[i],output_state,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=c.inputs[i];from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource=c.owned[i];to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        const D3D12_BOX box{xs[i],ys[i],0,xs[i]+widths[i],ys[i]+heights[i],1};
        const auto input_desc=ID3D12Resource_GetDesc(c.inputs[i]);
        ID3D12GraphicsCommandList_CopyTextureRegion(list,&to,0,0,0,&from,
            (input_desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)!=0?nullptr:&box);
        Transition(list,c.inputs[i],input_state,static_cast<D3D12_RESOURCE_STATES>(before[i]));
        Transition(list,c.owned[i],output_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        c.owned_state[i]=static_cast<unsigned>(output_state);
    }
    s.depth_physical_width=depth_copy_width;s.depth_physical_height=depth_copy_height;
    if(shader_snapshot||!full_depth)s.depth_x=s.depth_y=0; // full DSV copy retains its real active subrect
    if(shader_snapshot){s.depth_format=static_cast<unsigned>(depth_copy_format);s.motion_format=static_cast<unsigned>(motion_copy_format);}
    if(native_state)native_state->Restore();
    s.motion_physical_width=s.motion_source_width;s.motion_physical_height=s.motion_source_height;s.motion_x=s.motion_y=0;
    s.motion_bound_width=s.motion_source_width;s.motion_bound_height=s.motion_source_height;s.resample_motion=false;
    // Keep the actual native-input jitter receipt. Present binding separately
    // marks that it is not an observed output-color jitter phase.
    {Lock lock;c.reserved=false;c.recorded=true;*selected=c;
        guides_awaiting_submission.store(true,std::memory_order_release);
        guides_awaiting_retirement.store(true,std::memory_order_release);}
    GuideCaptureStage("recorded");return true;
}
// Called immediately after the real intercepted Execute, while the Core still
// serializes that queue's Execute/Signal transaction. It adds a completion fence
// only for recordings which actually contain one of our guide copies.
void GuideCopiesSubmitted(ID3D12CommandQueue* queue,Recording recording,bool succeeded) {
    for(auto& c:guide_copies)if(c.recorded&&!c.submitted&&c.recording==recording){
        if(!succeeded||!queue||!c.producer_fence){c.poisoned=true;continue;}
        if(c.producer_value==UINT64_MAX){c.poisoned=true;continue;}
        c.queue=QueueOf(queue);if(!c.queue){c.poisoned=true;continue;}++c.producer_value;
        if(FAILED(ID3D12CommandQueue_Signal(queue,c.producer_fence,c.producer_value))){c.poisoned=true;++guide_receipt_diagnostics.signal_failure;continue;}
        c.submitted=true;++guide_receipt_diagnostics.submitted;
    }
    bool pending=false;for(const auto& c:guide_copies)pending|=c.recorded&&!c.submitted&&!c.poisoned;
    guides_awaiting_submission.store(pending,std::memory_order_release);
}
// CPU-only advisory consumer hint; not source-frame proof or GPU completion.
nr_present_mapping::Demands<8> present_demands;
Id present_demand_skips=0;
bool PresentAutomaticViewportScale() { return true; }
bool PresentDemandNeedsCapture(Source source,unsigned config,unsigned width,unsigned height) {
    Lock lock;
    const auto* stream=ReceiptStreamFor(source,false);
    const bool needed=!stream || present_demands.Needed(stream->device,config,
        {width,height},true);
    if(!needed && present_demand_skips!=UINT64_MAX)++present_demand_skips;
    return needed;
}

nr_late_present::Selection AcquirePresentGuides(Id device,unsigned width,unsigned height,
    bool known_generated,bool known_fg_consumed,bool known_inline_processed,Id present_queue,unsigned config_generation) {
    nr_late_present::Candidate candidates[8]{};
    for(unsigned i=0;i<8;++i){const auto& c=guide_copies[i];const auto* stream=ReceiptStreamFor(c.source,false);
        const auto* device=ReceiptDeviceFor(c.recording.device,false);
        candidates[i]={c.source.stream,c.source.epoch,c.source.sequence,stream?stream->latest_sequence:0,
            c.opportunity,device?device->latest:0,c.recording.device,c.queue,c.producer_value,c.config_generation,
            c.output_width,c.output_height,c.recorded&&!c.reserved&&!c.acquired,c.native_result_known,
            c.native_succeeded,c.submitted,c.consumed,c.inline_processed,c.fg_consumed,c.poisoned||c.retired};}
    nr_late_present::Request request{device,width,height,config_generation,
        known_generated,known_fg_consumed,known_inline_processed};
    request.automatic_full_viewport_scale=PresentAutomaticViewportScale();
    auto selected=nr_late_present::Select(request,candidates,8);
    guide_receipt_diagnostics.present_width=width;guide_receipt_diagnostics.present_height=height;
    guide_receipt_diagnostics.selection=selected.decision;

    if(!selected)return selected;
    auto& c=guide_copies[selected.index];
    const auto done=ID3D12Fence_GetCompletedValue(c.producer_fence);if(done==UINT64_MAX){c.poisoned=true;
        selected.decision=nr_late_present::Decision::NativeFailed;guide_receipt_diagnostics.selection=selected.decision;return selected;}

    float mapped_x=c.guides.signature.motion_scale_x,mapped_y=c.guides.signature.motion_scale_y;
    if(!nr_present_mapping::ScaleToModel(mapped_x,mapped_y,
            {c.output_width,c.output_height},{width,height})) {
        selected.decision=nr_late_present::Decision::WrongExtent;
        guide_receipt_diagnostics.selection=selected.decision;return selected;
    }
    ++guide_receipt_diagnostics.selected;
    c.acquired=true;present_guides=&c;present_lease.source=c.source;
    present_lease.source_id=c.source.sequence;present_lease.role=Role::Application;present_lease.guides=c.guides;
    present_lease.source_signature=c.source_signature;present_lease.guides_owned=present_lease.guides_current=true;
    // O -> actual P only. No texture resample and no extra model pass.
    present_lease.guides.signature.motion_scale_x=mapped_x;
    present_lease.guides.signature.motion_scale_y=mapped_y;
    present_lease.source_jitter_known=c.guides.jitter.known;present_lease.output_jitter_known=false;
    present_lease.producer_fence=c.producer_fence;present_lease.producer_value=c.producer_value;
    present_lease.producer_queue=c.queue;present_lease.queue=present_queue;present_lease.source_age=selected.age;
    present_lease.cross_queue_wait=c.queue!=present_queue;present_lease.output_width=c.output_width;present_lease.output_height=c.output_height;
    return selected;
}
bool BeginPresentReceiptWork(ID3D12CommandQueue* queue) {
    present_blocked_by_fg=false;
    if(!present_guides||!present_ticket||!queue)return false;
    if(g_fg_presentation_policy.Read().late_owned){present_blocked_by_fg=true;return false;}
    auto& c=*present_guides;
    // Consumption is irreversible at the first private GPU operation. A failed
    // queue wait or later opaque failure must never retry this source.
    c.consumed=true;
    if(!claims.Record(present_ticket))return false;
    present_recorded=true;
    if(c.queue==QueueOf(queue))return true;
    if(!c.producer_fence||!c.producer_value||FAILED(ID3D12CommandQueue_Wait(queue,c.producer_fence,c.producer_value))){
        c.poisoned=true;return false;
    }
    return true;
}
bool PresentWorkBlockedByFg(){return present_blocked_by_fg;}
void ReleasePresentGuides(ID3D12Fence* fence,Id value,bool recorded) {
    if(!present_guides)return;auto& c=*present_guides;
    if(recorded){if(!fence||!value){c.poisoned=true;}
        else {ReleaseCom(c.consumer_fence);c.consumer_fence=fence;ID3D12Fence_AddRef(fence);c.consumer_value=value;}}
    c.acquired=false;present_guides=nullptr;
}
