// Included after the compat Present and FG owners. Called by the F8 worker only.
#include "nr_allocation_ledger.h"
#include <memory>
#include <new>
namespace nr_allocation_runtime {
using namespace nr_allocation_ledger;

void CoreSlot(Builder& ledger,const NrSlot& slot,Phase phase,bool auxiliary) {
    const auto core=[&](ID3D12Resource* value){ledger.Resource(Category::CoreSlot,phase,value);};
    const auto experimental=[&](ID3D12Resource* value){ledger.Resource(Category::Experiment,phase,value);};
    const auto capture=[&](ID3D12Resource* value){ledger.Resource(Category::Capture,phase,value);};
    // Native input/output and exposure are game-owned AddRefs. Auxiliary proxy
    // and neural refer to the separately owned dual spatial allocation.
    ledger.Resource(Category::CoreSlot,phase,slot.source,
        slot.owns_source?Ownership::Owned:Ownership::Borrowed);
    ledger.Resource(Category::CoreSlot,phase,slot.result,
        slot.owns_result?Ownership::Owned:Ownership::Borrowed);
    ledger.Resource(Category::CoreSlot,phase,slot.exposure_source,Ownership::Borrowed);
    ledger.Resource(Category::CoreSlot,phase,slot.motion_source,Ownership::Borrowed);
    ledger.Resource(Category::CoreSlot,phase,slot.proxy,
        auxiliary?Ownership::Borrowed:Ownership::Owned);
    ledger.Resource(Category::CoreSlot,phase,slot.neural,
        auxiliary?Ownership::Borrowed:Ownership::Owned);
    core(slot.motion_proxy);
    capture(slot.delta_stats);capture(slot.delta_stats_readback);
    capture(slot.acceptance_time_readback);capture(slot.dual_time_readback);
    capture(slot.stage_capture.buffer);capture(slot.stage_capture.readback);
    experimental(slot.edit_field);experimental(slot.edit_horizontal);
    experimental(slot.lanczos_intermediate);
    experimental(slot.region_guide);experimental(slot.region_field);
    experimental(slot.temporal_depth);experimental(slot.temporal_motion);
    experimental(slot.rr_depth);
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
void CoreContext(Builder& ledger,const NrContext& context,Phase phase) {
    if(!context.sr_handle)return;
    if(context.nr_handle)ledger.Model(context.layer_index,phase);
    if(context.quarantined_create_handle)ledger.Unknown(Category::CoreSlot,phase,Heap::Other);
    if(context.private_sr)ledger.Unknown(Category::PrivateSr,phase,Heap::Default);
    if(context.temporal)ledger.Unknown(Category::Temporal,phase,Heap::Default);
    if(context.binding_pool)ledger.Unknown(Category::Pool,phase,Heap::Other);
    for(const auto& slot:context.slots)CoreSlot(ledger,slot,phase,context.layer_index!=0);
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
    for(const auto& context:g_contexts)
        CoreContext(*ledger,context,context.retired?Phase::Retired:Phase::Live);
    for(const auto& context:g_pending_destroy_contexts)
        CoreContext(*ledger,context,Phase::Pending);
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
    // Sample has released g_chain_cs before taking the report lock. Evaluate
    // observation may still append to this published session on another thread.
    AcquireSRWLockExclusive(&g_feedback_lock);
    char ticks[160]{};
    if(nr_allocation_ledger::FormatSampleTicks(ticks,sizeof(ticks),prefix,started,finished))
        FeedbackAppend(session,"%s",ticks);
    nr_allocation_ledger::WriteText(snapshot,prefix,[&](const char* line){FeedbackAppend(session,"%s",line);});
    ReleaseSRWLockExclusive(&g_feedback_lock);
}
