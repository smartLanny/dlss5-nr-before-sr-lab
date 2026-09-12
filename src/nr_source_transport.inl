// Same-recording, owned guide copies. No CPU waits, borrowed texture history,
// guessed states, or copies on an unrelated presentation queue.
struct GuideCopy {
    Source source{};Recording recording{};NrGuideBinding guides{};
    ID3D12Resource* inputs[2]{};ID3D12Resource* owned[2]{};
    ID3D12Fence *producer_fence=nullptr,*consumer_fence=nullptr;
    Id producer_value=0,consumer_value=0,queue=0,bytes=0;
    unsigned owned_state[2]{};
    bool reserved=false,recorded=false,submitted=false,acquired=false,poisoned=false;
};
GuideCopy guide_copies[8]{};
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
bool CaptureGuides(ID3D12GraphicsCommandList* list,Source source,const NrGuideBinding& input) {
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_acquire)||!source||!list||!input.depth||!input.motion||input.signature.linear_depth)return false;
    const auto depth_desc=ID3D12Resource_GetDesc(input.depth);
    if((depth_desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)!=0 &&
       (input.signature.depth_x||input.signature.depth_y||input.signature.depth_width!=depth_desc.Width||input.signature.depth_height!=depth_desc.Height))return false;
    GuideCopy* selected=nullptr;unsigned before[2]{};Recording recording{};
    {
        Lock lock;recording=RecordingOf(list);if(!recording)return false;
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
    c.source=source;c.recording=recording;c.recorded=c.submitted=false;c.queue=0;
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
    // Present follows reconstruction/post-processing; raw camera jitter is not
    // automatically the phase of the presented color plane.
    c.guides.jitter.known=false;
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
void AcquirePresentGuides(Source source,Id queue) {
    for(auto& c:guide_copies)if(c.source==source&&c.recorded&&c.submitted&&!c.poisoned&&!c.reserved&&c.queue==queue){
        const auto done=ID3D12Fence_GetCompletedValue(c.producer_fence);if(done==UINT64_MAX){c.poisoned=true;continue;}
        // A later read on the identical queue is ordered even before CPU sees
        // completion. The consumer fence below prevents subsequent pool reuse.
        c.acquired=true;present_guides=&c;present_lease.guides=c.guides;
        present_lease.guides_owned=present_lease.guides_current=true;return;
    }
}
void ReleasePresentGuides(ID3D12Fence* fence,Id value,bool recorded) {
    if(!present_guides)return;auto& c=*present_guides;
    if(recorded){if(!fence||!value){c.poisoned=true;}
        else {ReleaseCom(c.consumer_fence);c.consumer_fence=fence;ID3D12Fence_AddRef(fence);c.consumer_value=value;}}
    c.acquired=false;present_guides=nullptr;
}
