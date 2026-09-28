// Session-only, same-list GPU timing. Called under g_chain_cs. No GPU waits.
namespace nr_acceptance_gpu {
constexpr unsigned kQueries=4,kRing=1024;
struct Sample {
    unsigned config=0,history=0,since_reset=0,work_width=0,work_height=0,output_width=0,output_height=0;
    unsigned long long instance=0,frame=0,queue=0,fence=0;
    nr_source_frame::Source source{};
    bool private_composed=false,temporal_recorded=false;
    double ms=0;
};
Sample samples[kRing]{};
unsigned read_index=0,write_index=0;
unsigned long long dropped=0;
bool Pop(Sample& out){
    if(read_index==write_index)return false;
    out=samples[read_index%kRing];++read_index;return true;
}
void Collect(NrSlot& slot){
    if(!slot.acceptance_time_pending)return;
    if(!slot.acceptance_time_queue||!slot.acceptance_time_fence)return;
    auto* queue=slot.acceptance_time_queue;
    if(!queue->fence)return;
    const auto completed=ID3D12Fence_GetCompletedValue(queue->fence);
    if(completed==UINT64_MAX||completed<slot.acceptance_time_fence)return;
    slot.acceptance_time_pending=false;
    if(!slot.acceptance_time_visible||slot.acceptance_time_mask!=15||!slot.acceptance_time_source||
       !slot.acceptance_time_readback||!queue->queue)return;
    UINT64 frequency=0;
    if(FAILED(ID3D12CommandQueue_GetTimestampFrequency(queue->queue,&frequency))||!frequency)return;
    UINT64 ticks[kQueries]{};void* mapped=nullptr;
    const D3D12_RANGE range{0,sizeof(ticks)};
    if(FAILED(ID3D12Resource_Map(slot.acceptance_time_readback,0,&range,&mapped))||!mapped)return;
    std::memcpy(ticks,mapped,sizeof(ticks));
    const D3D12_RANGE empty{0,0};ID3D12Resource_Unmap(slot.acceptance_time_readback,0,&empty);
    if(ticks[1]<ticks[0]||ticks[3]<ticks[2])return;
    const double ms=double((ticks[1]-ticks[0])+(ticks[3]-ticks[2]))*1000.0/double(frequency);
    if(!std::isfinite(ms)||ms<0||ms>10000||!slot.acceptance_time_identity.frame_id)return;
    if(write_index-read_index>=kRing){++dropped;return;}
    samples[write_index%kRing]={slot.acceptance_time_generation,slot.acceptance_time_history,
        slot.acceptance_time_identity.since_reset,slot.acceptance_time_work_width,
        slot.acceptance_time_work_height,slot.acceptance_time_output_width,
        slot.acceptance_time_output_height,slot.acceptance_time_identity.instance_id,
        slot.acceptance_time_identity.frame_id,queue->identity,slot.acceptance_time_fence,
        slot.acceptance_time_source,slot.acceptance_time_private_composed,
        slot.acceptance_time_temporal_recorded,ms};
    ++write_index;
}
bool Prepare(ID3D12Device* device,NrSlot& slot,ID3D12GraphicsCommandList* list,
             unsigned work_width,unsigned work_height,unsigned output_width,unsigned output_height){
    Collect(slot);
    if(slot.acceptance_time_pending)return false;
    if(!nr_acceptance::PerformanceActive()||!device||!list||!work_width||!work_height||!output_width||!output_height||
       nr_f8::IsActive()||StageCaptureStatus()!=0||nr_replay::Active())return false;
    if(!CanMutateSlotLocked(slot))return false;
    slot.acceptance_time_active=false;slot.acceptance_time_mask=0;
    slot.acceptance_time_token=slot.acceptance_time_fence=0;
    slot.acceptance_time_queue=nullptr;slot.acceptance_time_visible=false;
    slot.acceptance_time_private_composed=false;
    slot.acceptance_time_temporal_recorded=false;
    const auto source=nr_source_runtime::CurrentSource();
    unsigned long long token=0;
    if(!source||!ReadLifecycleToken(list,token)||!token)return false;
    if(!slot.acceptance_time_heap){
        D3D12_QUERY_HEAP_DESC d{};d.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;d.Count=kQueries;
        if(FAILED(ID3D12Device_CreateQueryHeap(device,&d,IID_ID3D12QueryHeap,
            reinterpret_cast<void**>(&slot.acceptance_time_heap))))return false;
    }
    if(!slot.acceptance_time_readback){
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=sizeof(UINT64)*kQueries;
        d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_READBACK;h.CreationNodeMask=h.VisibleNodeMask=1;
        if(FAILED(ID3D12Device_CreateCommittedResource(device,&h,D3D12_HEAP_FLAG_NONE,&d,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,
            reinterpret_cast<void**>(&slot.acceptance_time_readback))))return false;
    }
    slot.acceptance_time_generation=g_config_generation.load();
    slot.acceptance_time_history=g_history_generation.load();
    slot.acceptance_time_work_width=work_width;slot.acceptance_time_work_height=work_height;
    slot.acceptance_time_output_width=output_width;slot.acceptance_time_output_height=output_height;
    slot.acceptance_time_source=source;slot.acceptance_time_token=token;
    slot.acceptance_time_active=true;return true;
}
void Stamp(ID3D12GraphicsCommandList* list,NrSlot& slot,unsigned query){
    if(!slot.acceptance_time_active||query>=kQueries||!slot.acceptance_time_heap)return;
    unsigned long long token=0;
    if(!ReadLifecycleToken(list,token)||token!=slot.acceptance_time_token||
       (slot.acceptance_time_mask&(1u<<query)))return;
    ID3D12GraphicsCommandList_EndQuery(list,slot.acceptance_time_heap,D3D12_QUERY_TYPE_TIMESTAMP,query);
    slot.acceptance_time_mask|=1u<<query;
}
void Finish(ID3D12GraphicsCommandList* list,NrSlot& slot,bool visible){
    if(!slot.acceptance_time_active)return;
    Stamp(list,slot,3);
    const unsigned required=g_config.nr_passes==2?
        (g_config.nr_full_reference?g_config.nr_full_passes:2u):1u;
    const bool all_layers=required==1||(required==2&&slot.dual_selected!=nullptr)||
        (required>2&&nr_dual_runtime::full_adopted.load()==required);
    slot.acceptance_time_visible=visible&&all_layers&&slot.acceptance_time_mask==15&&
        StageCaptureStatus()==0&&!nr_f8::IsActive()&&!nr_replay::Active();
    if(slot.acceptance_time_mask==15)
        ID3D12GraphicsCommandList_ResolveQueryData(list,slot.acceptance_time_heap,
            D3D12_QUERY_TYPE_TIMESTAMP,0,kQueries,slot.acceptance_time_readback,0);
    slot.acceptance_time_pending=slot.acceptance_time_mask==15;
    slot.acceptance_time_active=false;
}
}
