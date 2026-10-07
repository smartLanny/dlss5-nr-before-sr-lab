// g_chain_cs protects these CPU-only values. Publication never grants release.
namespace {
struct NrRetirementObserved {
    nr_retirement::Id instance=0,first=0,fingerprint=0,eval_frame=0,retired_tick=0;
    nr_retirement::Id submit_queue=0,submit_value=0,complete_queue=0,complete_value=0;
    unsigned retire_reason=0;
    bool post_evaluated=false,post_ready=false;
};
NrRetirementObserved g_nr_retirement_observed[nr_retirement::kContexts]{};
struct NrRetirementList {nr_retirement::Id token=0,address=0;};
NrRetirementList g_nr_retirement_lists[nr_retirement::kRecordings]{};
struct NrRetirementFence {ID3D12Fence* object=nullptr;nr_retirement::Id completed=0,tick=0,publication=0;};
NrRetirementFence g_nr_retirement_fences[kLifecycleQueueCount]{};
const NrContext* g_nr_retirement_deciding=nullptr;
nr_retirement::Snapshot g_nr_retirement_published{},g_nr_retirement_building{};
bool g_nr_retirement_dirty=false;
nr_retirement::Id g_nr_retirement_observations=0,g_nr_retirement_suppressed=0;
nr_retirement::Id g_nr_retirement_publication=0;

bool NrRetirementRegistered(nr_retirement::Id id) {
    for(const auto& c:g_contexts)if(c.sr_handle&&c.instance_id==id)return true;
    for(const auto& c:g_pending_destroy_contexts)if(c.sr_handle&&c.instance_id==id)return true;
    return false;
}
NrRetirementObserved* NrRetirementObservedFor(const NrContext& c) {
    if(!c.instance_id)return nullptr;
    for(auto& e:g_nr_retirement_observed)if(e.instance==c.instance_id)return &e;
    for(auto& e:g_nr_retirement_observed)if(!e.instance||!NrRetirementRegistered(e.instance)) {
        e={};e.instance=c.instance_id;return &e;
    }
    return nullptr;
}
nr_retirement::Id NrRetirementFingerprint(const NrContext& c) {
    nr_retirement::Id h=c.instance_id;
    const auto add=[&](nr_retirement::Id v){h=(h^v)*1099511628211ull;};
    add(c.recording_command_lists);add(c.pending_recording_token);add(c.awaiting_fence_signal);
    add(c.completion_fence_value);add(reinterpret_cast<nr_retirement::Id>(c.last_submission_queue));
    add(c.create_dependencies_quarantined);add(c.lifecycle_dependencies_quarantined);
    add(c.temporal_token);add(reinterpret_cast<nr_retirement::Id>(c.temporal));
    add(reinterpret_cast<nr_retirement::Id>(c.private_sr));
    for(const auto& s:c.slots){add(reinterpret_cast<nr_retirement::Id>(s.key));
        add(s.recording_command_lists);add(s.awaiting_fence_signal);add(s.completion_fence_value);}
    if(c.binding_pool)for(const auto& p:c.binding_pool->packets){add(p.valid);
        add(p.lease.recording_command_lists);add(p.lease.awaiting_fence_signal);add(p.lease.completion_fence_value);}
    return h;
}
nr_retirement::Fence NrRetirementReadFence(LifecycleQueue* q,unsigned long long value,
                                         nr_retirement::Id tick,bool can_sample) {
    nr_retirement::Fence out{};out.value=value;if(q){out.queue=q->identity;out.object=reinterpret_cast<nr_retirement::Id>(q->fence);}
    if(!value||!out.object)return out;
    for(const auto& f:g_nr_retirement_fences)if(f.object==reinterpret_cast<ID3D12Fence*>(out.object)&&f.publication==g_nr_retirement_publication) {
        out.completed=f.completed;out.sampled=true;out.reused=true;out.sample_tick=f.tick;return out;
    }
    if(can_sample){out.completed=ID3D12Fence_GetCompletedValue(reinterpret_cast<ID3D12Fence*>(out.object));
        out.sampled=true;out.sample_tick=tick;}
    return out;
}
template<class T> nr_retirement::Lease NrRetirementReadLease(const T& source,nr_retirement::Id tick,bool sample) {
    nr_retirement::Lease out{};out.recording=source.recording_command_lists;out.awaiting_signal=source.awaiting_fence_signal;
    out.fence=NrRetirementReadFence(source.last_submission_queue,source.completion_fence_value,tick,sample);return out;
}
void NrRetirementLeaseMask(nr_retirement::Context& c,const nr_retirement::Lease& lease,
    nr_retirement::Id recording,nr_retirement::Id awaiting,nr_retirement::Id fence) {
    c.evaluated|=recording|awaiting;
    if(lease.recording)c.blocked|=recording;
    if(lease.awaiting_signal)c.blocked|=awaiting;
    if(!lease.fence.value)c.evaluated|=fence;
    else if(!lease.fence.object){c.evaluated|=fence;c.blocked|=fence;}
    else if(lease.fence.sampled){c.evaluated|=fence;
        if(lease.fence.completed==UINT64_MAX||lease.fence.completed<lease.fence.value)c.blocked|=fence;}
    else c.unknown|=fence;
}
const NrContext* NrRetirementSlotOwner(const void* slot,unsigned& index) {
    for(const auto& c:g_contexts)if(c.sr_handle)for(unsigned i=0;i<std::size(c.slots);++i)
        if(&c.slots[i]==slot){index=i;return &c;}
    for(const auto& c:g_pending_destroy_contexts)if(c.sr_handle)for(unsigned i=0;i<std::size(c.slots);++i)
        if(&c.slots[i]==slot){index=i;return &c;}
    return nullptr;
}
}

void NrRetirementEvaluationLocked(const NrContext& c,unsigned long long frame) {
    if(auto* e=NrRetirementObservedFor(c))e->eval_frame=frame;
}
void NrRetirementMarkedLocked(const NrContext& c,unsigned reason) {
    if(auto* e=NrRetirementObservedFor(c)){if(!e->retired_tick)e->retired_tick=GetTickCount64();
        if(!e->retire_reason)e->retire_reason=reason;g_nr_retirement_dirty=true;}
}
void NrRetirementBeginDecisionLocked(const NrContext& c) {
    g_nr_retirement_deciding=&c;
    if(auto* e=NrRetirementObservedFor(c)){
        const auto fingerprint=NrRetirementFingerprint(c);
        if(e->fingerprint!=fingerprint)g_nr_retirement_dirty=true;
        e->fingerprint=fingerprint;e->post_evaluated=false;e->post_ready=false;
        if(!e->retired_tick)e->retired_tick=GetTickCount64();
    }
}
void NrRetirementDecisionLocked(const NrContext& c,nr_retirement::Id first,bool post_evaluated,bool ready) {
    if(g_nr_retirement_deciding!=&c)return;
    if(auto* e=NrRetirementObservedFor(c)){
        if(e->first!=first)g_nr_retirement_dirty=true;
        e->first=first;e->post_evaluated=post_evaluated;e->post_ready=ready;
    }
}
void NrRetirementEndDecisionLocked(){g_nr_retirement_deciding=nullptr;}
void NrRetirementFenceObservedLocked(ID3D12Fence* fence,unsigned long long completed) {
    if(!g_nr_retirement_deciding)return;
    NrRetirementFence* free=nullptr;
    for(auto& f:g_nr_retirement_fences){if(f.object==fence){free=&f;break;}if(!f.object)free=&f;}
    if(free){free->object=fence;free->completed=completed;free->tick=GetTickCount64();free->publication=g_nr_retirement_publication+1;}
}
void NrRetirementContextFenceObservedLocked(const NrContext& c,unsigned long long completed) {
    if(g_nr_retirement_deciding!=&c||completed==UINT64_MAX||completed<c.completion_fence_value||!c.last_submission_queue)return;
    if(auto* e=NrRetirementObservedFor(c)){e->complete_queue=c.last_submission_queue->identity;e->complete_value=c.completion_fence_value;}
}
void NrRetirementRecordListLocked(const LifecycleCommandList& record,ID3D12GraphicsCommandList* list) {
    const auto index=&record-g_lifecycle_command_lists;
    if(index>=0&&static_cast<std::size_t>(index)<std::size(g_nr_retirement_lists))
        g_nr_retirement_lists[index]={record.token,reinterpret_cast<nr_retirement::Id>(list)};
}
void NrRetirementSubmittedLocked(const NrContext& c,const LifecycleQueue& q,unsigned long long value) {
    if(auto* e=NrRetirementObservedFor(c)){e->submit_queue=q.identity;e->submit_value=value;}
}

void NrRetirementPublishLocked(bool collector_uncertain) {
    using namespace nr_retirement;
    // Reuse only values read by the original decisions before this publication,
    // never a prior collector that happened to run in the same clock millisecond.
    ++g_nr_retirement_publication;
    bool has_retired=false;
    for(const auto& c:g_contexts)has_retired|=c.sr_handle&&c.retired;
    unsigned pending_used=0;
    for(const auto& c:g_pending_destroy_contexts){has_retired|=c.sr_handle&&c.retired;pending_used+=c.sr_handle!=nullptr;}
    const bool tracking=g_lifecycle_tracking_uncertain.load(std::memory_order_acquire);
    const unsigned releasing=g_nr_context_releases_in_progress.load(std::memory_order_acquire);
    if(tracking!=g_nr_retirement_published.tracking_uncertain||pending_used!=g_nr_retirement_published.pending_used||releasing!=g_nr_retirement_published.releasing)g_nr_retirement_dirty=true;
    // No holder/owner scans at all on the ordinary, non-retired path.
    if(!has_retired){if(g_nr_retirement_published.contexts||g_nr_retirement_dirty){std::memset(&g_nr_retirement_published,0,sizeof(g_nr_retirement_published));
            g_nr_retirement_published.sequence=++g_nr_retirement_observations;
            g_nr_retirement_published.pending_capacity=static_cast<unsigned>(std::size(g_pending_destroy_contexts));
            g_nr_retirement_published.pending_used=pending_used;g_nr_retirement_published.releasing=releasing;g_nr_retirement_published.tracking_uncertain=tracking;
            g_nr_retirement_dirty=false;}return;}
    const Id tick=GetTickCount64();++g_nr_retirement_observations;
    if(!g_nr_retirement_dirty&&g_nr_retirement_published.sequence&&tick-g_nr_retirement_published.tick<1000){++g_nr_retirement_suppressed;return;}
    auto& out=g_nr_retirement_building;std::memset(&out,0,sizeof(out));out.sequence=g_nr_retirement_published.sequence+1;out.tick=tick;
    out.observations=g_nr_retirement_observations;out.suppressed=g_nr_retirement_suppressed;
    out.tracking_uncertain=tracking;
    out.pending_capacity=static_cast<unsigned>(std::size(g_pending_destroy_contexts));
    out.releasing=releasing;out.pending_used=pending_used;
    const NrContext* owners[kContexts]{};
    // Aggregate holder relationships independently of the bounded detail table.
    Id indirect_binding_contexts[kRecordings]{};
    const auto append=[&](const NrContext& c,bool pending){
        if(!c.sr_handle||!c.retired)return;
        if(out.contexts==kContexts){++out.context_overflow;return;}
        const unsigned n=out.contexts++;owners[n]=&c;auto& row=out.context[n];
        row.instance=c.instance_id;row.parent=c.parent_instance_id;row.feature=c.feature_generation;
        row.global_codec_device=reinterpret_cast<Id>(g_codec.device);row.handle=reinterpret_cast<Id>(c.sr_handle);
        row.configuration=c.config_generation;row.layer=c.layer_index;row.mode=c.mode;row.width=c.width;row.height=c.height;
        row.pending_destroy=pending;row.pending_token=c.pending_recording_token;row.last_eval=c.evaluations;
        const bool sample=!out.tracking_uncertain&&!c.create_dependencies_quarantined&&!c.lifecycle_dependencies_quarantined;
        row.lease=NrRetirementReadLease(c,tick,sample);
        row.evaluated=CreateQuarantine|LifecycleQuarantine|TrackingUncertain|ContextPending|PendingCapacity|ReleaseInProgress;
        if(c.create_dependencies_quarantined)row.blocked|=CreateQuarantine;
        if(c.lifecycle_dependencies_quarantined)row.blocked|=LifecycleQuarantine;
        if(out.tracking_uncertain)row.blocked|=TrackingUncertain;
        if(c.pending_recording_token)row.blocked|=ContextPending;
        if(!pending&&out.pending_used==out.pending_capacity)row.blocked|=PendingCapacity;
        if(out.releasing)row.blocked|=ReleaseInProgress;
        NrRetirementLeaseMask(row,row.lease,ContextRecording,ContextAwaitSignal,ContextFence);
        if(auto* e=NrRetirementObservedFor(c)){row.first_block=collector_uncertain?TrackingUncertain:e->first;
            row.last_eval_frame=e->eval_frame;row.last_submit_queue=e->submit_queue;row.last_submit_value=e->submit_value;
            row.last_completed_queue=e->complete_queue;row.last_completed_value=e->complete_value;
            row.retired_tick=e->retired_tick;row.retire_reason=e->retire_reason;
            row.post_evaluated=!collector_uncertain&&e->post_evaluated;row.post_ready=row.post_evaluated&&e->post_ready;
        }
        if(row.post_evaluated){row.evaluated|=PostReady;if(!row.post_ready)row.blocked|=PostReady;}else row.unknown|=PostReady;
        row.evaluated|=SlotRecording|SlotAwaitSignal|SlotFence|Bindings|BindingRecording|BindingAwaitSignal|BindingFence|OtherContextBinding;
        for(unsigned i=0;i<std::size(c.slots);++i)if(c.slots[i].key){
            if(out.slots==kSlots){++out.slot_overflow;row.unknown|=SlotRecording|SlotAwaitSignal|SlotFence;continue;}
            auto& s=out.slot[out.slots++];s.context=n;s.index=i;s.key=reinterpret_cast<Id>(c.slots[i].key);
            s.lease=NrRetirementReadLease(c.slots[i],tick,sample);
            NrRetirementLeaseMask(row,s.lease,SlotRecording,SlotAwaitSignal,SlotFence);
        }
        const auto inspect=[&](OwnerInspection state,bool temporal){
            const Id busy=temporal?TemporalRecording:PrivateRecording,unsafe=temporal?TemporalUnsafe:PrivatePoison;
            row.evaluated|=busy|unsafe;if((!temporal&&state.poisoned)||state.unsafe)row.blocked|=unsafe;
            if(state.opaque_unknown&&!temporal&&!row.post_evaluated)row.unknown|=PostReady;
            for(unsigned i=0;i<state.count&&i<std::size(state.flights);++i){auto f=state.flights[i];f.instance=c.instance_id;
                if(temporal){f.queue=c.temporal_queue;f.fence.queue=f.queue;}
                if(f.token||f.unsafe)row.blocked|=busy;
                if(f.fence.object&&sample){f.fence.completed=ID3D12Fence_GetCompletedValue(reinterpret_cast<ID3D12Fence*>(f.fence.object));f.fence.sampled=true;f.fence.sample_tick=tick;}
                if(f.fence.value){
                    if(!f.fence.object)row.blocked|=busy;
                    else if(!f.fence.sampled)row.unknown|=busy;
                    else if(f.fence.completed==UINT64_MAX||f.fence.completed<f.fence.value)row.blocked|=busy;
                }
                auto& count=temporal?out.temporal_flights:out.private_flights;
                auto& overflow=temporal?out.temporal_overflow:out.private_overflow;
                if(count==kOwnerFlights){++overflow;row.unknown|=busy;continue;}
                (temporal?out.temporal_flight:out.private_flight)[count++]=f;
            }
        };
        inspect(nr_post_product::InspectRetirement(c),false);
        OwnerInspection temporal{};
        if(c.temporal){const auto state=c.temporal->inspect_retirement();temporal.present=state.present;
            temporal.poisoned=state.faulted;temporal.unsafe=state.unsafe;temporal.opaque_unknown=state.opaque_unknown;
            temporal.count=std::min<unsigned>(state.count,std::size(temporal.flights));
            for(unsigned i=0;i<temporal.count;++i){const auto& source=state.flights[i];auto& f=temporal.flights[i];
                f.index=source.index;f.token=source.recording;f.fence.object=source.fence;f.fence.value=source.value;
                f.submitted=source.submitted;f.unsafe=source.unsafe;}}
        inspect(temporal,true);
    };
    for(const auto& c:g_contexts)append(c,false);
    for(const auto& c:g_pending_destroy_contexts)append(c,true);
    const auto bindings=[&](const NrContext& owner,bool live){if(!owner.sr_handle||!owner.binding_pool)return;
        for(unsigned p=0;p<std::size(owner.binding_pool->packets);++p){const auto& packet=owner.binding_pool->packets[p];if(!packet.valid)continue;
            unsigned primary_slot=0,secondary_slot=0;
            const auto* primary=NrRetirementSlotOwner(packet.plan.primary_owner,primary_slot);
            const auto* secondary=NrRetirementSlotOwner(packet.plan.secondary_owner,secondary_slot);
            if(!owner.retired&&!(primary&&primary->retired)&&!(secondary&&secondary->retired))continue;
            const bool sample=!out.tracking_uncertain&&!owner.create_dependencies_quarantined&&!owner.lifecycle_dependencies_quarantined;
            const auto lease=NrRetirementReadLease(packet.lease,tick,sample);Id related=0;
            for(unsigned c=0;c<out.contexts;++c){auto& row=out.context[c];
                if(owners[c]==&owner||owners[c]==primary||owners[c]==secondary)related|=1ull<<c;
                if(owners[c]==&owner){const Id before=row.blocked;NrRetirementLeaseMask(row,lease,BindingRecording,BindingAwaitSignal,BindingFence);
                    if((row.blocked^before)&(BindingRecording|BindingAwaitSignal|BindingFence))row.blocked|=Bindings;
                }else if(live&&(owners[c]==primary||owners[c]==secondary)){
                    Context probe{};NrRetirementLeaseMask(probe,lease,BindingRecording,BindingAwaitSignal,BindingFence);
                    if(probe.blocked)row.blocked|=OtherContextBinding;if(probe.unknown)row.unknown|=OtherContextBinding;
                }
            }
            for(unsigned r=0;r<std::size(g_lifecycle_command_lists);++r){const auto& record=g_lifecycle_command_lists[r];if(!record.token)continue;
                for(unsigned s=0;s<std::min<unsigned>(record.slot_count,std::size(record.slots));++s)
                    if(record.slots[s]==&packet.lease)indirect_binding_contexts[r]|=related;
            }
            // Only text/POD packet detail can be omitted. Neither the aggregate
            // lease truth nor its actual list holder depends on this capacity.
            if(out.bindings==kBindings){++out.binding_overflow;continue;}
            auto& b=out.binding[out.bindings++];b.instance=owner.instance_id;b.feature=owner.feature_generation;
            b.configuration=owner.config_generation;b.index=p;b.primary_known=primary!=nullptr;b.secondary_known=secondary!=nullptr;
            if(primary){b.primary_instance=primary->instance_id;b.primary_slot=primary_slot;}
            if(secondary){b.secondary_instance=secondary->instance_id;b.secondary_slot=secondary_slot;}
            b.lease=lease;
        }
    };
    for(const auto& c:g_contexts)bindings(c,true);
    for(const auto& c:g_pending_destroy_contexts)bindings(c,false);
    for(unsigned i=0;i<std::size(g_lifecycle_command_lists);++i){const auto& record=g_lifecycle_command_lists[i];if(!record.token)continue;
        Id direct=0,indirect=indirect_binding_contexts[i];for(unsigned r=0;r<std::min<unsigned>(record.context_count,std::size(record.contexts));++r)
            for(unsigned c=0;c<out.contexts;++c)if(record.contexts[r]==owners[c])direct|=1ull<<c;
        for(unsigned s=0;s<std::min<unsigned>(record.slot_count,std::size(record.slots));++s){
            for(unsigned c=0;c<out.contexts;++c)for(const auto& owned:owners[c]->slots)
                if(record.slots[s]==&owned)indirect|=1ull<<c;
        }
        const Id matches=direct|indirect;
        if(!matches)continue;
        if(out.recordings==kRecordings){++out.recording_overflow;continue;}
        auto& r=out.recording[out.recordings++];r.token=record.token;r.contexts=matches;r.direct_contexts=direct;r.indirect_contexts=indirect;r.expected_queue=record.expected_queue_identity;
        if(g_nr_retirement_lists[i].token==record.token)r.address=g_nr_retirement_lists[i].address;
        r.submissions=record.submission_count;r.in_flight=record.in_flight_submissions;r.context_count=record.context_count;r.slot_count=record.slot_count;
        r.detached=record.detached;r.unproven_execution=record.execution_attempted_without_proof;
    }
    for(unsigned i=0;i<out.contexts;++i){auto& row=out.context[i];row.evaluated&=~row.unknown;
        row.unknown|=kAll&~(row.evaluated|row.unknown);}
    const bool changed=!g_nr_retirement_published.sequence||g_nr_retirement_dirty;
    g_nr_retirement_published=out;g_nr_retirement_dirty=false;
    if(changed&&out.contexts){const auto& first=out.context[0];
        Log("[nr-retirement-block] sequence=%llu instance=%llu first=%s blocked=0x%llX evaluated=0x%llX unknown=0x%llX contexts=%u records=%u pending=%u/%u releasing=%u; frozen CPU evidence, no release permission.",
            out.sequence,first.instance,Name(first.first_block),first.blocked,first.evaluated,first.unknown,out.contexts,out.recordings,out.pending_used,out.pending_capacity,out.releasing);}
}

void SnapshotNrRetirementEvidence(nr_retirement::Snapshot& out) {
    EnterCriticalSection(&g_chain_cs);out=g_nr_retirement_published;LeaveCriticalSection(&g_chain_cs);
}
