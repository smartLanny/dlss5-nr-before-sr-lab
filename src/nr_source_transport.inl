// Same-recording, owned guide copies. No CPU waits, borrowed texture history,
// guessed states, or copies on an unrelated presentation queue.
struct GuideCopy {
    Source source{};Recording recording{};NrGuideBinding guides{};
    NrGuideSignature source_signature{};
    ID3D12Resource* inputs[2]{};ID3D12Resource* owned[2]{};
    ID3D12Fence *producer_fence=nullptr,*consumer_fence=nullptr;
    Id producer_value=0,consumer_value=0,queue=0,bytes=0,opportunity=0;
    unsigned output_width=0,output_height=0,config_generation=0;
    unsigned owned_state[2]{};
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
    unsigned output_width,unsigned output_height,unsigned config_generation) {
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_acquire)||!source||!list||!input.depth||!input.motion||
       input.signature.linear_depth||!output_width||!output_height||output_width>16384||output_height>16384)return false;
    const auto depth_desc=ID3D12Resource_GetDesc(input.depth);
    if((depth_desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)!=0 &&
       (input.signature.depth_x||input.signature.depth_y||input.signature.depth_width!=depth_desc.Width||input.signature.depth_height!=depth_desc.Height))return false;
    GuideCopy* selected=nullptr;unsigned before[2]{};Recording recording{};
    {
        Lock lock;recording=RecordingOf(list);if(!recording)return false;
        const auto* stream=ReceiptStreamFor(source,false);
        if(!stream||stream->device!=recording.device||stream->latest_sequence!=source.sequence||!stream->latest_opportunity)return false;
        for(auto& copy:guide_copies)if(copy.source==source&&copy.recorded&&!copy.poisoned)return true;
        if(!StateOf(recording,ResourceOf(input.depth),before[0])||!StateOf(recording,ResourceOf(input.motion),before[1]))return false;
        for(auto& copy:guide_copies)if(GuideReusable(copy)){selected=&copy;break;}
        if(!selected)return false;
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
    bool reuse=made&&c.owned[0]&&c.owned[1]&&c.producer_fence;
    if(reuse){const auto d=ID3D12Resource_GetDesc(c.owned[0]),m=ID3D12Resource_GetDesc(c.owned[1]);
        const auto dm=ID3D12Resource_GetDesc(input.depth),mm=ID3D12Resource_GetDesc(input.motion);
        ID3D12Device* owner=nullptr;
        reuse=SUCCEEDED(ID3D12Resource_GetDevice(c.owned[0],IID_ID3D12Device,reinterpret_cast<void**>(&owner)))&&
            IsSameRrDepthDevice(device,owner)&&d.Width==input.signature.depth_width&&d.Height==input.signature.depth_height&&d.Format==dm.Format&&
            m.Width==input.signature.motion_source_width&&m.Height==input.signature.motion_source_height&&m.Format==mm.Format;
        ReleaseCom(owner);}
    if(!reuse){ClearGuideCopy(c);c.reserved=true;
        Id all_bytes=0;{Lock lock;for(const auto& copy:guide_copies)if(&copy!=selected)all_bytes+=copy.bytes;}
        c.bytes=all_bytes;
        if(made)made=MakeGuideCopy(device,input.depth,input.signature.depth_width,input.signature.depth_height,c.owned[0],c.bytes)&&
            MakeGuideCopy(device,input.motion,input.signature.motion_source_width,input.signature.motion_source_height,c.owned[1],c.bytes)&&
            SUCCEEDED(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(&c.producer_fence)));
        c.bytes-=all_bytes;
    }else {for(auto& resource:c.inputs)ReleaseCom(resource);ReleaseCom(c.consumer_fence);c.consumer_value=0;}
    ReleaseCom(device);
    if(!made){ClearGuideCopy(c);{Lock lock;*selected={};}ReleaseSRWLockExclusive(&guide_allocation_lock);return false;}
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
    for(unsigned i=0;i<2;++i){
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
    s.depth_physical_width=s.depth_width;s.depth_physical_height=s.depth_height;s.depth_x=s.depth_y=0;
    s.motion_physical_width=s.motion_source_width;s.motion_physical_height=s.motion_source_height;s.motion_x=s.motion_y=0;
    s.motion_bound_width=s.motion_source_width;s.motion_bound_height=s.motion_source_height;s.resample_motion=false;
    // Keep the actual native-input jitter receipt. Present binding separately
    // marks that it is not an observed output-color jitter phase.
    {Lock lock;c.reserved=false;c.recorded=true;*selected=c;
        guides_awaiting_submission.store(true,std::memory_order_release);
        guides_awaiting_retirement.store(true,std::memory_order_release);}return true;
}
// Called immediately after the real intercepted Execute, while the Core still
// serializes that queue's Execute/Signal transaction. It adds a completion fence
// only for recordings which actually contain one of our guide copies.
void GuideCopiesSubmitted(ID3D12CommandQueue* queue,Recording recording,bool succeeded) {
    for(auto& c:guide_copies)if(c.recorded&&!c.submitted&&c.recording==recording){
        if(!succeeded||!queue||!c.producer_fence){c.poisoned=true;continue;}
        if(c.producer_value==UINT64_MAX){c.poisoned=true;continue;}
        c.queue=QueueOf(queue);if(!c.queue){c.poisoned=true;continue;}++c.producer_value;
        if(FAILED(ID3D12CommandQueue_Signal(queue,c.producer_fence,c.producer_value))){c.poisoned=true;continue;}
        c.submitted=true;
    }
    bool pending=false;for(const auto& c:guide_copies)pending|=c.recorded&&!c.submitted&&!c.poisoned;
    guides_awaiting_submission.store(pending,std::memory_order_release);
}
nr_late_present::Selection AcquirePresentGuides(Id device,unsigned width,unsigned height,
    bool known_generated,bool known_fg_consumed,bool known_inline_processed,Id present_queue,unsigned config_generation) {
    nr_late_present::Candidate candidates[8]{};
    for(unsigned i=0;i<8;++i){const auto& c=guide_copies[i];const auto* stream=ReceiptStreamFor(c.source,false);
        const auto* device=ReceiptDeviceFor(c.recording.device,false);
        candidates[i]={c.source.stream,c.source.epoch,c.source.sequence,stream?stream->latest_sequence:0,
            c.opportunity,device?device->latest:0,c.recording.device,c.queue,c.producer_value,c.config_generation,
            c.output_width,c.output_height,c.recorded,c.native_result_known,
            c.native_succeeded,c.submitted,c.consumed,c.inline_processed,c.fg_consumed,c.poisoned||c.retired};}
    const nr_late_present::Request request{device,width,height,config_generation,
        known_generated,known_fg_consumed,known_inline_processed};
    auto selected=nr_late_present::Select(request,candidates,8);
    if(!selected)return selected;
    auto& c=guide_copies[selected.index];
    const auto done=ID3D12Fence_GetCompletedValue(c.producer_fence);if(done==UINT64_MAX){c.poisoned=true;
        selected.decision=nr_late_present::Decision::NativeFailed;return selected;}
    c.acquired=true;present_guides=&c;present_lease.source=c.source;
    present_lease.source_id=c.source.sequence;present_lease.role=Role::Application;present_lease.guides=c.guides;
    present_lease.source_signature=c.source_signature;present_lease.guides_owned=present_lease.guides_current=true;
    present_lease.source_jitter_known=c.guides.jitter.known;present_lease.output_jitter_known=false;
    present_lease.producer_fence=c.producer_fence;present_lease.producer_value=c.producer_value;
    present_lease.producer_queue=c.queue;present_lease.queue=present_queue;present_lease.source_age=selected.age;
    present_lease.cross_queue_wait=c.queue!=present_queue;present_lease.output_width=c.output_width;present_lease.output_height=c.output_height;
    return selected;
}
bool BeginPresentReceiptWork(ID3D12CommandQueue* queue) {
    if(!present_guides||!present_ticket||!queue)return false;
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
void ReleasePresentGuides(ID3D12Fence* fence,Id value,bool recorded) {
    if(!present_guides)return;auto& c=*present_guides;
    if(recorded){if(!fence||!value){c.poisoned=true;}
        else {ReleaseCom(c.consumer_fence);c.consumer_fence=fence;ID3D12Fence_AddRef(fence);c.consumer_value=value;}}
    c.acquired=false;present_guides=nullptr;
}
