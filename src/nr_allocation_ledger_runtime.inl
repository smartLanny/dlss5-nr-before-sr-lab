// Included after the compat Present and FG owners. Called by the F8 worker only.
#include "nr_allocation_ledger.h"
#include <memory>
#include <new>
namespace nr_allocation_runtime {
using namespace nr_allocation_ledger;

void CoreSlot(Builder& ledger,const NrSlot& slot,Phase phase,bool auxiliary) {
    const auto core=[&](ID3D12Resource* value,Role role=Role::Other){ledger.ResourceRole(role);ledger.Resource(Category::CoreSlot,phase,value);};
    const auto experimental=[&](ID3D12Resource* value){ledger.Resource(Category::Experiment,phase,value);};
    const auto capture=[&](ID3D12Resource* value){ledger.Resource(Category::Capture,phase,value);};
    // Native input/output and exposure are game-owned AddRefs. Auxiliary proxy
    // and neural refer to the separately owned dual spatial allocation.
    ledger.ResourceRole(Role::Source);ledger.Resource(Category::CoreSlot,phase,slot.source,
        slot.owns_source?Ownership::Owned:Ownership::Borrowed);
    ledger.ResourceRole(Role::Result);ledger.Resource(Category::CoreSlot,phase,slot.result,
        slot.owns_result?Ownership::Owned:Ownership::Borrowed);
    ledger.Resource(Category::CoreSlot,phase,slot.exposure_source,Ownership::Borrowed);
    ledger.Resource(Category::CoreSlot,phase,slot.motion_source,Ownership::Borrowed);
    ledger.ResourceRole(Role::Proxy);ledger.Resource(Category::CoreSlot,phase,slot.proxy,
        auxiliary?Ownership::Borrowed:Ownership::Owned);
    ledger.ResourceRole(Role::NeuralOutput);ledger.Resource(Category::CoreSlot,phase,slot.neural,
        auxiliary?Ownership::Borrowed:Ownership::Owned);
    core(slot.motion_proxy,Role::MotionProxy);
    ledger.ResourceRole(Role::Other);
    capture(slot.delta_stats);capture(slot.delta_stats_readback);
    capture(slot.acceptance_time_readback);capture(slot.dual_time_readback);
    capture(slot.stage_capture.buffer);capture(slot.stage_capture.readback);
    experimental(slot.edit_field);experimental(slot.edit_horizontal);
    ledger.ResourceRole(Role::SRIntermediate);experimental(slot.lanczos_intermediate);
    ledger.ResourceRole(Role::Other);
    experimental(slot.region_guide);experimental(slot.region_field);
    experimental(slot.temporal_depth);experimental(slot.temporal_motion);
    ledger.ResourceRole(Role::RrDepth);experimental(slot.rr_depth);ledger.ResourceRole(Role::Other);
    experimental(slot.spatial_scratch);experimental(slot.spatial_low_b);
    experimental(slot.spatial_low_delta);
    // face_mask, region_inputs, dual_guides, temporal_*_input and spatial
    // bound inputs are references into other owners; never register aliases.
    if(slot.dual_spatial){
        const auto surfaces=nr_dual::dx12::GetSurfaces(slot.dual_spatial);
        ForEachOwnedSpatialResource(surfaces,[&](ID3D12Resource* resource){
            ledger.Resource(Category::Dual,phase,resource);
        });
    }
}
void CoreContext(Builder& ledger,NrContext& context,Phase phase) {
    if(!context.sr_handle)return;
    auto* detail=ledger.BeginContext(context.instance_id,context.feature_generation,context.sr_handle,
        context.nr_handle,context.layer_index,context.mode,phase,
        context.create_dependencies_quarantined||context.lifecycle_dependencies_quarantined,context.memory_max_slots);
    if(context.nr_handle)ledger.Model(context.layer_index,phase);
    if(context.quarantined_create_handle)ledger.Unknown(Category::CoreSlot,phase,Heap::Other);
    if(context.private_sr)ledger.Unknown(Category::PrivateSr,phase,Heap::Default);
    if(context.temporal)ledger.Unknown(Category::Temporal,phase,Heap::Default);
    if(context.binding_pool)ledger.Unknown(Category::Pool,phase,Heap::Other);
    for(unsigned i=0;i<kSlots;++i){
        ledger.BeginSlot(i,context.slots[i].key!=nullptr);
        CoreSlot(ledger,context.slots[i],phase,context.layer_index!=0);
    }
    if(detail){
        context.memory_sampled_high_water=std::max(context.memory_sampled_high_water,detail->total.bytes);
        detail->sampled_high_water=context.memory_sampled_high_water;
    }
    ledger.EndContext();
}
void CompatContext(Builder& ledger,const nr_compat_present_runtime::Context& context,Category category) {
    const auto own=[&](ID3D12Resource* value){ledger.Resource(category,Phase::Live,value);};
    own(context.raw);own(context.unknown_depth);own(context.unknown_motion);
    own(context.image.source);own(context.image.result);
    own(context.image.proxy);own(context.image.neural);
    for(const auto& flight:context.flights)own(flight.timing_readback);
    for(unsigned i=0;i<context.passes&&i<nr_compat_present_runtime::kPassLimit;++i){
        const auto& layer=context.layers[i];
        if(layer.context.nr_handle)ledger.Model(i,Phase::Live);
        if(layer.spatial){
            const auto surfaces=nr_dual::dx12::GetSurfaces(layer.spatial);
            ForEachOwnedSpatialResource(surfaces,[&](ID3D12Resource* resource){
                ledger.Resource(category,Phase::Live,resource);
            });
        }
    }
#if NR_HAS_TEMPORAL_RUNTIME
    if(context.temporal)ledger.Unknown(Category::Temporal,Phase::Live,Heap::Default);
#endif
    // c.bytes covers only part of the context and is deliberately reported
    // separately by the existing compat feedback, never added here.
}
Snapshot Sample() {
    std::unique_ptr<Builder> ledger(new(std::nothrow) Builder);
    if(!ledger){
        Snapshot unavailable{};
        unavailable.sample_complete=false;
        unavailable.cells[unsigned(Category::CoreSlot)][unsigned(Phase::Live)][unsigned(Heap::Other)].unknown=1;
        return unavailable;
    }
    EnterCriticalSection(&g_chain_cs);
    for(auto& context:g_contexts)
        CoreContext(*ledger,context,context.retired?Phase::Retired:Phase::Live);
    for(auto& context:g_pending_destroy_contexts)
        CoreContext(*ledger,context,Phase::Pending);
    const auto extra_budget_bytes=g_extra_allocated_bytes;
    const unsigned destroying=g_nr_context_releases_in_progress.load(std::memory_order_acquire);
    for(unsigned i=0;i<destroying;++i){
        ledger->Destroying();
        ledger->Unknown(Category::PrivateSr,Phase::Destroying,Heap::Default);
        ledger->Unknown(Category::Temporal,Phase::Destroying,Heap::Default);
        ledger->Unknown(Category::Dual,Phase::Destroying,Heap::Default);
        ledger->Unknown(Category::Pool,Phase::Destroying,Heap::Other);
    }
    if(nr_compat_present_runtime::current)
        CompatContext(*ledger,*nr_compat_present_runtime::current,Category::Compat);
    // RetireCurrent temporarily removes current while Release runs without
    // the chain lock. No zero-resource claim is valid in that interval.
    ledger->CompatRetiring(nr_compat_present_runtime::retirement_active);
    // A failed outside-lock DestroyContext can leave a quarantined owner
    // detached from both tables. The sticky signal gives no safe exact count.
    ledger->DetachedQuarantine(g_lifecycle_tracking_uncertain.load(std::memory_order_acquire));
    for(const auto& session:nr_fg_runtime::sessions)if(session.stream){
        CompatContext(*ledger,session.images,Category::Fg);
        ledger->Resource(Category::Fg,Phase::Live,session.full_raw);
        ledger->Resource(Category::Fg,Phase::Live,session.alpha_raw);
        ledger->Resource(Category::Fg,Phase::Live,session.scene_encoded);
    }
    LeaveCriticalSection(&g_chain_cs);
    // These owners use their own worker/retirement protocols. Their existing
    // budget counters are payloads or mixed SDK figures, not allocInfo totals.
    if(nr_f8_budget::gpu_bytes.load(std::memory_order_acquire)||
       nr_replay::resident_bytes.load(std::memory_order_acquire))
        ledger->Unknown(Category::Capture,Phase::Live,Heap::Other);
    if(nr_face_gpu::allocated_bytes.load(std::memory_order_acquire))
        ledger->Unknown(Category::Experiment,Phase::Live,Heap::Other);
    auto result=ledger->Take();
    result.extra_budget_bytes=extra_budget_bytes;
    result.dual_budget_bytes=g_dual_allocated_bytes.load(std::memory_order_acquire);
    result.private_budget_bytes=nr_private_sr_status::bytes.load(std::memory_order_acquire);
    result.temporal_budget_bytes=nr_temporal_status::bytes.load(std::memory_order_acquire);
    result.capture_budget_bytes=nr_f8_budget::gpu_bytes.load(std::memory_order_acquire);
    result.replay_budget_bytes=nr_replay::resident_bytes.load(std::memory_order_acquire);
    result.face_budget_bytes=nr_face_gpu::allocated_bytes.load(std::memory_order_acquire);
    // Several owners have no safe resource-level enumeration seam yet. The
    // lower bound is useful, but must never be presented as a complete total.
    result.sample_complete=false;
    return result;
}
} // namespace nr_allocation_runtime

void NrAppendAllocationLedger(FeedbackSession& session,const char* prefix) {
    const auto started=GetTickCount64();
    const auto snapshot=nr_allocation_runtime::Sample();
    const auto finished=GetTickCount64();
    nr_model_memory_events::History events;
    AcquireSRWLockShared(&g_nr_model_memory_lock);
    events=g_nr_model_memory_history;
    ReleaseSRWLockShared(&g_nr_model_memory_lock);
    // Sample has released g_chain_cs before taking the report lock. Evaluate
    // observation may still append to this published session on another thread.
    AcquireSRWLockExclusive(&g_feedback_lock);
    char ticks[160]{};
    if(nr_allocation_ledger::FormatSampleTicks(ticks,sizeof(ticks),prefix,started,finished))
        FeedbackAppend(session,"%s",ticks);
    nr_allocation_ledger::WriteText(snapshot,prefix,[&](const char* line){FeedbackAppend(session,"%s",line);});
    char event_row[1024]{};
    FeedbackAppend(session,"%s.allocation.modelEventCoverage=count:%u,overwritten:%llu; latest 32 cold events; absent history is missing\n",
        prefix,events.count,static_cast<unsigned long long>(events.overwritten));
    for(unsigned i=0;i<events.count;++i)
        if(nr_model_memory_events::Format(event_row,sizeof(event_row),prefix,events.events[i]))FeedbackAppend(session,"%s",event_row);
    ReleaseSRWLockExclusive(&g_feedback_lock);
}

void NrObserveMemoryTrend(reshade::api::effect_runtime* runtime) {
    // No resource enumeration and no additional GPU work. Cheap cadence only
    // while enabled; process query and logging are outside every Core lock.
    static SRWLOCK cadence_lock=SRWLOCK_INIT;
    static nr_memory_observation::Cadence cadence;
    if(!TryAcquireSRWLockExclusive(&cadence_lock))return;
    const auto tick=GetTickCount64();
    const bool due=cadence.Due(g_enabled_fast.load(std::memory_order_acquire),tick);
    ReleaseSRWLockExclusive(&cadence_lock);
    if(!due)return;
    nr_memory_observation::Trend trend;trend.tick=tick;
    if(!TryEnterCriticalSection(&g_chain_cs))return;
    trend.config=g_config_generation.load();trend.history=g_history_generation.load();
    const auto count=[&](const NrContext& context,bool pending){
        if(!context.sr_handle)return;
        if(pending)++trend.pending;else if(context.retired)++trend.retired;else ++trend.live;
        if(context.create_dependencies_quarantined||context.lifecycle_dependencies_quarantined)++trend.quarantine;
        trend.slots+=nr_memory_observation::SlotCount(context.slots);
    };
    for(const auto& context:g_contexts)count(context,false);
    for(const auto& context:g_pending_destroy_contexts)count(context,true);
    trend.uncertain=g_lifecycle_tracking_uncertain.load(std::memory_order_acquire);
    trend.destroying=g_nr_context_releases_in_progress.load(std::memory_order_acquire);
    trend.timing=g_nr_memory_gpu_window.Read(trend.config,trend.history);
    trend.timing_key=trend.timing.count?g_nr_memory_gpu_window.Key():nr_memory_observation::TimingKey{};
    g_nr_memory_gpu_window.Clear(); // Never report a previous interval as fresh.
    LeaveCriticalSection(&g_chain_cs);
    LUID adapter{};
    auto* device=runtime->get_device();
    const bool known=device&&device->get_property(reshade::api::device_properties::adapter_luid,&adapter);
    const auto process=nr_process_memory::Read(adapter,known,0,false);
    trend.sample_tick=process.tick;trend.adapter_low=adapter.LowPart;trend.adapter_high=unsigned(adapter.HighPart);
    trend.dxgi_result=unsigned(process.local_result);
    trend.dxgi_valid=SUCCEEDED(process.local_result);
    if(trend.dxgi_valid){trend.usage=process.local.CurrentUsage;trend.budget=process.local.Budget;}
    char row[1024]{};
    if(nr_memory_observation::FormatTrend(row,sizeof(row),trend))Log("%s",row);
}
