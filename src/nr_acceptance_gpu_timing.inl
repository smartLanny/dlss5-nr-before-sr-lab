// Session-only, same-list GPU timing. Called under g_chain_cs. No GPU waits.
namespace nr_acceptance_gpu {
constexpr unsigned kQueries=4,kRing=1024;
enum class Diagnostic : unsigned { SlotBusy, Admission, Source, Heap, Readback, BindToken,
    BindOwner, BindSource, StampToken, StampDuplicate, Incomplete, Invisible,
    AwaitSubmit, AwaitFence, DeviceRemoved, RetiredInvalid, RingFull, NativeOrFinish,
    SrEntry, RrEntry, SrPrepareReached, RrPrepareReached,
    SrPreRoute, SrPreLifecycle, SrPreOwner, SrPreUnavailable,
    RrPreRoute, RrPreLifecycle, RrPreOwner, RrPreUnavailable, Count };
constexpr const char* DiagnosticName(Diagnostic d){
    switch(d){
    case Diagnostic::SlotBusy:return "slot-busy";case Diagnostic::Admission:return "admission";
    case Diagnostic::Source:return "source";case Diagnostic::Heap:return "query-heap";
    case Diagnostic::Readback:return "readback";case Diagnostic::BindToken:return "bind-token";
    case Diagnostic::BindOwner:return "bind-owner";case Diagnostic::BindSource:return "bind-source";
    case Diagnostic::StampToken:return "stamp-token";case Diagnostic::StampDuplicate:return "stamp-duplicate";
    case Diagnostic::Incomplete:return "incomplete";case Diagnostic::Invisible:return "invisible";
    case Diagnostic::AwaitSubmit:return "await-submit";case Diagnostic::AwaitFence:return "await-fence";
    case Diagnostic::DeviceRemoved:return "device-removed";case Diagnostic::RetiredInvalid:return "retired-invalid";
    case Diagnostic::RingFull:return "ring-full";case Diagnostic::NativeOrFinish:return "native-or-finish-skipped";
    case Diagnostic::SrEntry:return "sr-entry";case Diagnostic::RrEntry:return "rr-entry";
    case Diagnostic::SrPrepareReached:return "sr-prepare-reached";case Diagnostic::RrPrepareReached:return "rr-prepare-reached";
    case Diagnostic::SrPreRoute:return "sr-preprepare-route";case Diagnostic::SrPreLifecycle:return "sr-preprepare-lifecycle";
    case Diagnostic::SrPreOwner:return "sr-preprepare-owner";case Diagnostic::SrPreUnavailable:return "sr-preprepare-unavailable";
    case Diagnostic::RrPreRoute:return "rr-preprepare-route";case Diagnostic::RrPreLifecycle:return "rr-preprepare-lifecycle";
    case Diagnostic::RrPreOwner:return "rr-preprepare-owner";case Diagnostic::RrPreUnavailable:return "rr-preprepare-unavailable";
    default:return "unknown";
    }
}

std::atomic<unsigned long long> diagnostics[static_cast<unsigned>(Diagnostic::Count)]{};
std::atomic<unsigned> diagnostic_config{0};
void Note(Diagnostic kind,const char* stage,unsigned config=0){
    if(!nr_acceptance::PerformanceActive()||(config&&config!=diagnostic_config.load(std::memory_order_relaxed)))return;
    const auto count=diagnostics[static_cast<unsigned>(kind)].fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=3 || count%600==0)Log("[acceptance-gpu] %s count=%llu; no GPU sample is claimed",
        stage,count);
}
thread_local EntryScope* current_entry=nullptr;
EntryScope::EntryScope(EntryRoute r,bool enabled):route(r){
    active=enabled&&nr_acceptance::PerformanceActive();
    if(!active)return;
    config=diagnostic_config.load(std::memory_order_relaxed);
    previous=current_entry;current_entry=this;
    Note(route==EntryRoute::Sr?Diagnostic::SrEntry:Diagnostic::RrEntry,"real-evaluate-entry",config);
}
EntryScope::~EntryScope(){
    if(!active)return;
    current_entry=previous;
    if(reached)return;
    const bool sr=route==EntryRoute::Sr;
    const Diagnostic d=reason==PreReason::Route?(sr?Diagnostic::SrPreRoute:Diagnostic::RrPreRoute):
        reason==PreReason::Lifecycle?(sr?Diagnostic::SrPreLifecycle:Diagnostic::RrPreLifecycle):
        reason==PreReason::Owner?(sr?Diagnostic::SrPreOwner:Diagnostic::RrPreOwner):
        (sr?Diagnostic::SrPreUnavailable:Diagnostic::RrPreUnavailable);
    Note(d,"real-entry-did-not-reach-timestamp-prepare",config);
}
void EntryScope::ReachPrepare(){
    if(!active||reached)return;
    reached=true;
    Note(route==EntryRoute::Sr?Diagnostic::SrPrepareReached:Diagnostic::RrPrepareReached,
        "real-entry-reached-timestamp-prepare",config);
}
void ReachPrepare(){if(current_entry)current_entry->ReachPrepare();}
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
    if(!slot.acceptance_time_queue||!slot.acceptance_time_fence){
        if(nr_acceptance::PerformanceActive())Note(Diagnostic::AwaitSubmit,"submitted-fence-unpublished",slot.acceptance_time_generation);
        return;
    }
    auto* queue=slot.acceptance_time_queue;
    if(!queue->fence){if(nr_acceptance::PerformanceActive())Note(Diagnostic::AwaitFence,"queue-fence-unavailable",slot.acceptance_time_generation);return;}
    const auto completed=ID3D12Fence_GetCompletedValue(queue->fence);
    if(completed==UINT64_MAX){Note(Diagnostic::DeviceRemoved,"queue-fence-device-removed",slot.acceptance_time_generation);return;}
    if(completed<slot.acceptance_time_fence){
        if(nr_acceptance::PerformanceActive())Note(Diagnostic::AwaitFence,"exact-fence-not-complete",slot.acceptance_time_generation);
        return;
    }
    slot.acceptance_time_pending=false;
    if(!slot.acceptance_time_visible){Note(Diagnostic::Invisible,"retired-hidden-bypass-or-partial-layer",slot.acceptance_time_generation);return;}
    if(slot.acceptance_time_mask!=15||!slot.acceptance_time_source||
       !slot.acceptance_time_readback||!queue->queue){Note(Diagnostic::RetiredInvalid,"retired-incomplete-proof",slot.acceptance_time_generation);return;}
    UINT64 frequency=0;
    if(FAILED(ID3D12CommandQueue_GetTimestampFrequency(queue->queue,&frequency))||!frequency){
        Note(Diagnostic::RetiredInvalid,"retired-frequency-unavailable",slot.acceptance_time_generation);return;
    }
    UINT64 ticks[kQueries]{};void* mapped=nullptr;
    const D3D12_RANGE range{0,sizeof(ticks)};
    if(FAILED(ID3D12Resource_Map(slot.acceptance_time_readback,0,&range,&mapped))||!mapped){
        Note(Diagnostic::RetiredInvalid,"retired-readback-unavailable",slot.acceptance_time_generation);return;
    }
    std::memcpy(ticks,mapped,sizeof(ticks));
    const D3D12_RANGE empty{0,0};ID3D12Resource_Unmap(slot.acceptance_time_readback,0,&empty);
    if(ticks[1]<ticks[0]||ticks[3]<ticks[2]){
        Note(Diagnostic::RetiredInvalid,"retired-timestamp-order",slot.acceptance_time_generation);return;
    }
    const double ms=double((ticks[1]-ticks[0])+(ticks[3]-ticks[2]))*1000.0/double(frequency);
    if(!std::isfinite(ms)||ms<=0||ms>10000||!slot.acceptance_time_identity.frame_id||
   !slot.acceptance_time_identity.instance_id||
   slot.acceptance_time_identity.config_generation!=slot.acceptance_time_generation){
        Note(Diagnostic::RetiredInvalid,"retired-duration-or-identity",slot.acceptance_time_generation);return;
    }
    if(write_index-read_index>=kRing){++dropped;Note(Diagnostic::RingFull,"sample-ring-full",slot.acceptance_time_generation);return;}
    samples[write_index%kRing]={slot.acceptance_time_generation,slot.acceptance_time_history,
        slot.acceptance_time_identity.since_reset,slot.acceptance_time_work_width,
        slot.acceptance_time_work_height,slot.acceptance_time_output_width,
        slot.acceptance_time_output_height,slot.acceptance_time_identity.instance_id,
        slot.acceptance_time_identity.frame_id,queue->identity,slot.acceptance_time_fence,
        slot.acceptance_time_source,slot.acceptance_time_private_composed,
        slot.acceptance_time_temporal_recorded,ms};
    ++write_index;
}
void DiscardUnsubmitted(unsigned long long token){
    // A successful native Reset discards this recording. Only its exact
    // unsubmitted token may release timestamps; submitted slots need the fence.
    auto* record=FindLifecycleCommandListLocked(token);
    if(!record||record->detached||record->submission_count||
       record->in_flight_submissions||record->execution_attempted_without_proof)return;
    for(unsigned i=0;i<record->slot_count;++i){
        auto* slot=record->slots[i];
        if(!slot||slot->acceptance_time_token!=token||
           slot->acceptance_time_queue||slot->acceptance_time_fence)continue;
        slot->acceptance_time_active=false;
        slot->acceptance_time_prepared=false;
        slot->acceptance_time_pending=false;
        slot->acceptance_time_mask=0;
        slot->acceptance_time_token=0;
        slot->acceptance_time_visible=false;
    }
}
void DiscardPrepared(NrSlot& slot){
    if(!slot.acceptance_time_prepared||slot.acceptance_time_pending)return;
    slot.acceptance_time_prepared=false;slot.acceptance_time_active=false;
    slot.acceptance_time_mask=0;slot.acceptance_time_token=0;
    slot.acceptance_time_source={};slot.acceptance_time_visible=false;
}
bool Prepare(ID3D12Device* device,NrSlot& slot,ID3D12GraphicsCommandList* list,
             unsigned work_width,unsigned work_height,unsigned output_width,unsigned output_height){
    if(!nr_acceptance::PerformanceActive())return false;
    Collect(slot);
    if(slot.acceptance_time_pending){Note(Diagnostic::SlotBusy,"previous-sample-awaits-exact-fence");return false;}
    if(!device||!list||!work_width||!work_height||!output_width||!output_height||
       nr_f8::IsActive()||StageCaptureStatus()!=0||nr_replay::Active()){
        Note(Diagnostic::Admission,"prepare-admission-unavailable");return false;
    }
    if(!CanMutateSlotLocked(slot)){Note(Diagnostic::SlotBusy,"mutable-slot-unavailable");return false;}
    slot.acceptance_time_prepared=false;slot.acceptance_time_active=false;slot.acceptance_time_mask=0;
    slot.acceptance_time_token=slot.acceptance_time_fence=0;
    slot.acceptance_time_identity={}; // Never reuse prior frame identity.
    slot.acceptance_time_queue=nullptr;slot.acceptance_time_visible=false;
    slot.acceptance_time_private_composed=false;
    slot.acceptance_time_temporal_recorded=false;
    const auto source=nr_source_runtime::CurrentSource();
    if(!source){Note(Diagnostic::Source,"prepare-source-unavailable");return false;}
    if(!slot.acceptance_time_heap){
        D3D12_QUERY_HEAP_DESC d{};d.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;d.Count=kQueries;
        if(FAILED(ID3D12Device_CreateQueryHeap(device,&d,IID_ID3D12QueryHeap,
            reinterpret_cast<void**>(&slot.acceptance_time_heap)))){
            Note(Diagnostic::Heap,"timestamp-heap-unavailable");return false;
        }
    }
    if(!slot.acceptance_time_readback){
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=sizeof(UINT64)*kQueries;
        d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_READBACK;h.CreationNodeMask=h.VisibleNodeMask=1;
        if(FAILED(ID3D12Device_CreateCommittedResource(device,&h,D3D12_HEAP_FLAG_NONE,&d,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,
            reinterpret_cast<void**>(&slot.acceptance_time_readback)))){
            Note(Diagnostic::Readback,"timestamp-readback-unavailable");return false;
        }
    }
    slot.acceptance_time_generation=g_config_generation.load();
    slot.acceptance_time_history=g_history_generation.load();
    slot.acceptance_time_work_width=work_width;slot.acceptance_time_work_height=work_height;
    slot.acceptance_time_output_width=output_width;slot.acceptance_time_output_height=output_height;
    slot.acceptance_time_source=source;
    slot.acceptance_time_prepared=true;return true;
}
bool Bind(ID3D12GraphicsCommandList* list,NrSlot& slot){
    if(!slot.acceptance_time_prepared)return false;
    unsigned long long token=0;
    if(!list||!ReadLifecycleToken(list,token)||!token){
        Note(Diagnostic::BindToken,"recorded-list-token-unavailable",slot.acceptance_time_generation);DiscardPrepared(slot);return false;
    }
    auto* record=FindLifecycleCommandListLocked(token);
    bool owns_slot=false;
    if(record&&!record->detached)for(unsigned i=0;i<record->slot_count;++i)
        if(record->slots[i]==&slot){owns_slot=true;break;}
    if(!owns_slot){Note(Diagnostic::BindOwner,"recorded-token-slot-mismatch",slot.acceptance_time_generation);DiscardPrepared(slot);return false;}
    if(nr_source_runtime::CurrentSource()!=slot.acceptance_time_source){
        Note(Diagnostic::BindSource,"recorded-source-changed",slot.acceptance_time_generation);DiscardPrepared(slot);return false;
    }
    slot.acceptance_time_token=token;slot.acceptance_time_prepared=false;
    slot.acceptance_time_active=true;return true;
}
void Abort(NrSlot& slot,const char* stage){
    if(!slot.acceptance_time_active)return;
    Note(Diagnostic::NativeOrFinish,stage,slot.acceptance_time_generation);
    // The lifecycle still owns resources until the recorded list retires.
    slot.acceptance_time_active=false;slot.acceptance_time_visible=false;
}
void Stamp(ID3D12GraphicsCommandList* list,NrSlot& slot,unsigned query){
    if(!slot.acceptance_time_active||query>=kQueries||!slot.acceptance_time_heap)return;
    unsigned long long token=0;
    if(!ReadLifecycleToken(list,token)||token!=slot.acceptance_time_token){
        Note(Diagnostic::StampToken,"stamp-recording-token-changed",slot.acceptance_time_generation);return;
    }
    if(slot.acceptance_time_mask&(1u<<query)){Note(Diagnostic::StampDuplicate,"stamp-query-repeated",slot.acceptance_time_generation);return;}
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
    if(slot.acceptance_time_mask!=15)Note(Diagnostic::Incomplete,"q0-q3-incomplete",slot.acceptance_time_generation);
    else if(!slot.acceptance_time_visible)Note(Diagnostic::Invisible,"finished-hidden-bypass-or-partial-layer",slot.acceptance_time_generation);
    slot.acceptance_time_active=false;
}
}
