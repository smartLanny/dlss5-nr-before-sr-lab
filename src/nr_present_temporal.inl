// Keep GPU-observed history statistics separate from submission receipts.
void CollectTemporalStats(Context& c) {
    if(c.temporal)nr_temporal_status::ObserveGpuStats(c.temporal->stats(),
        c.temporal_seen,g_config_generation.load());
}
// Called under g_chain_cs; includes retained poisoned Present allocations.
void AppendTemporalMemory(nr_temporal::Memory& out) {
    if(!current||!current->temporal)return;
    const auto bytes=current->temporal->stats().bytes;
    ++out.live;out.live_bytes+=bytes;
    if(current->timeline.Poisoned()){
        ++out.quarantined;out.quarantined_bytes+=bytes;
    }
}
// Present owns its queue, lists and fence. A generic native-NGX context is
// deliberately not fabricated for this consumer. Included after Present types.
void EndTemporalRecording(Context& c,Flight& f) {
    if(c.temporal&&f.temporal_recording){
        // Called only after successful native Reset, or final Release of this
        // private list after its final GPU fence. A completed fence alone is not enough.
        if(f.temporal_pending){
            if(!c.temporal->cancelled_unsubmitted()){
                Poison(c,0xBAD00005u,"temporal-unsubmitted-cancellation");return;
            }
            nr_temporal_status::cancelled.fetch_add(1);
        }
        c.temporal->end_recording(f.temporal_recording);
    }
    f.temporal_recording=0;f.temporal_pending=false;
}
bool ReleaseTemporal(Context& c) {
    if(!c.temporal)return true;
    if(!c.temporal->releasable())return false;
    CollectTemporalStats(c);
    nr_temporal_status::bytes.fetch_sub(c.temporal->stats().bytes);
    delete c.temporal;c.temporal=nullptr;c.temporal_previous={};c.temporal_seen={};
    return true;
}
bool DrainTemporal(Context& c) {
    if(!c.temporal)return true;
    if(c.timeline.Poisoned())return false;
    const auto done=ID3D12Fence_GetCompletedValue(c.fence);
    if(done==UINT64_MAX){Poison(c,0xBAD00002u,"temporal-device-removed");return false;}
    for(auto& f:c.flights)if(f.temporal_recording){
        if(f.fence&&done<f.fence)return false;
        if(FAILED(ID3D12CommandAllocator_Reset(f.allocator))||
           FAILED(ID3D12GraphicsCommandList_Reset(f.list,f.allocator,nullptr))){
            Poison(c,0xBAD00002u,"temporal-drain-Reset");return false;
        }
        EndTemporalRecording(c,f);
        if(c.timeline.Poisoned()||FAILED(ID3D12GraphicsCommandList_Close(f.list))){
            Poison(c,0xBAD00002u,"temporal-drain-Close");return false;
        }
    }
    return ReleaseTemporal(c);
}
void MaintainTemporal(Context& c) {
    if(!c.temporal)return;
    if(!g_config.temporal_backend||g_config.temporal_backend!=2||g_config.temporal_scope||
       c.temporal_mode!=g_config.temporal_mode)
        (void)DrainTemporal(c);
}
bool SubmitTemporal(Context& c,Flight& f,unsigned long long fence) {
    if(!f.temporal_pending)return true;
    if(!c.temporal||!c.temporal->submitted(c.fence,fence,f.temporal_recording)){
        Poison(c,0xBAD00005u,"temporal-actual-submission-receipt");return false;
    }
    f.temporal_pending=false;c.temporal_previous=c.temporal_pending_source;
    nr_temporal_status::submitted.fetch_add(1);
    nr_temporal_status::last_tick.store(GetTickCount64());
    nr_temporal_status::state.store(8); // queued result, not pixel history-adoption proof
    return true;
}
bool RecordTemporal(Context& c,Flight& f) {
    using namespace nr::final_residual;
    if(!g_config.temporal_backend)return true; // no flow, allocation or extra capture
    CollectTemporalStats(c);
    const auto decline=[](unsigned reason){nr_temporal_status::state.store(reason);
        nr_temporal_status::rejected.fetch_add(1);return true;};
    if(g_config.temporal_backend!=2)return decline(19);
    if(g_config.temporal_scope)return decline(18);
    if(c.encoding!=Encoding::Srgb)return decline(10);
    const auto& lease=nr_source_runtime::CurrentPresentLease();
    if(!lease.source||!lease.color_lineage_proven||lease.role!=nr_source_frame::Role::Application||
       lease.output_width!=c.source.width||lease.output_height!=c.source.height||
       g_fg_presentation_policy.Read().late_owned)return decline(22);
    if(g_config.light_preview||g_config.region_preview||g_config.face_preview||
       g_config.colour_neutral_probe)return decline(10);
    if(c.temporal&&c.temporal_mode!=g_config.temporal_mode)return decline(3);
    if(!c.temporal){
        const auto now=GetTickCount64();if(now<c.temporal_retry_tick)return decline(3);
        c.temporal_retry_tick=now+2000;
        const auto mem=nr_process_memory::Read(ID3D12Device_GetAdapterLuid(c.device),true);
        if(FAILED(mem.local_result)||mem.local.CurrentUsage>=mem.local.Budget)return decline(3);
        const auto spare=mem.local.Budget-mem.local.CurrentUsage;
        const auto budget=std::min<unsigned long long>(768ull<<20,spare>64ull<<20?spare-(64ull<<20):0);
        c.temporal=new(std::nothrow)RecordedTemporal(c.device,c.queue,c.source.width,c.source.height,
            Backend::AmdOpticalFlow,Scope::WholeChain,budget);
        if(!c.temporal||!c.temporal->initialized()){
            delete c.temporal;c.temporal=nullptr;return decline(3);
        }
        c.temporal_mode=g_config.temporal_mode;
        nr_temporal_status::Allocated(c.temporal->stats().bytes);
    }
    if(!c.temporal->can_record())return decline(6);
    if(c.temporal_serial==UINT64_MAX)return decline(3);
    RecordedInputs in;
    LARGE_INTEGER ticks{},frequency{};QueryPerformanceCounter(&ticks);QueryPerformanceFrequency(&frequency);
    const auto time=static_cast<unsigned long long>(
        static_cast<long double>(ticks.QuadPart)*1000000.0L/frequency.QuadPart);
    in.frame={{lease.source.stream,lease.source.epoch,lease.source.sequence},
        {c.generation,(static_cast<unsigned long long>(g_config_generation.load())<<32)|g_history_generation.load(),
         kResidualLinearSdr,c.source.width,c.source.height,Backend::AmdOpticalFlow,Scope::WholeChain,
         {0,0,c.source.width,c.source.height,0,c.generation}},time,
         lease.guides_current&&lease.guides.game_reset!=0};
    in.frame.source_to_scene=1.f;
    in.frame.previous_source=c.temporal_previous;
    in.settings.enabled=true;in.settings.scope=Scope::WholeChain;
    in.settings.mode=g_config.temporal_mode?Mode::LowFrequency:Mode::Adaptive;
    // Low-frequency reconstruction requires its neighborhood path; retain the saved legacy choice for ordinary mode.
 in.settings.validation=(g_config.temporal_legacy&&!g_config.temporal_mode)?Validation::Legacy:Validation::Basic065;
    in.layers=c.passes;
    in.original=c.image.source;in.original_state=c.image.source_state;
    in.candidate=c.image.result;in.candidate_state=c.image.result_state;
    in.candidate_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    const auto before=c.temporal->stats().bytes;
    const auto result=c.temporal->record(f.list,in);
    const auto after=c.temporal->stats().bytes;
    if(after>before)nr_temporal_status::Allocated(after-before);
    if(result==RecordResult::Recorded){
        f.temporal_recording=++c.temporal_serial;f.temporal_pending=true;
        c.temporal_pending_source=in.frame.source;
        nr_temporal_status::recorded.fetch_add(1);nr_temporal_status::state.store(7);return true;
    }
    if(result==RecordResult::Faulted){
        Poison(c,0xBAD00005u,"temporal-opaque-recording");nr_temporal_status::state.store(9);return false;
    }
    return decline(result==RecordResult::Duplicate?13:result==RecordResult::Busy?6:12);
}
