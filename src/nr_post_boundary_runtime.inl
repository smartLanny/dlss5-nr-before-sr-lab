#include "nr_post_boundary.h"
#define NR_HAS_POST_BOUNDARY_TRACE 1
extern thread_local bool g_inside_ngx_evaluate;

SRWLOCK g_post_boundary_lock=SRWLOCK_INIT;
std::atomic<std::uint64_t> g_post_boundary_resource{0};
std::atomic<std::uint64_t> g_post_boundary_device{0};
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
std::atomic<std::uint64_t> g_post_boundary_descriptor_resource{0};
std::uint64_t NrDescriptorResourceToken(std::uint64_t);
#endif
nr_post_boundary::Window g_post_boundary_current;
nr_post_boundary::Window g_post_boundary_history[nr_post_boundary::kHistory];
unsigned g_post_boundary_history_next=0;
std::uint64_t g_post_boundary_next_trace=0;
std::atomic<std::uint64_t> g_post_boundary_order{0};
constexpr std::uint64_t kPostBoundaryMaxAgeMs=500;

// Implemented beside the existing native hook families. Never query COM or
// publish hooks in a command detour; the registry supplies bounded identity.
nr_post_boundary::List SnapshotPostBoundaryList(ID3D12GraphicsCommandList *);
unsigned EnsurePostBoundaryNativeHooks(ID3D12GraphicsCommandList *);

nr_probe_policy::Snapshot PostBoundaryProbePolicy() {
#if NR_HAS_PROBE_POLICY
    return NrProbePolicySnapshot();
#else
    return {};
#endif
}
void ClosePostBoundaryLocked(nr_post_boundary::Close reason) {
    if(g_post_boundary_current.frame) {
        g_post_boundary_current.close=reason;
        g_post_boundary_current.closed_tick=GetTickCount64();
        if(!g_post_boundary_current.origin_submitted)
            g_post_boundary_current.coverage|=nr_post_boundary::SubmissionUnknown;
        g_post_boundary_history[g_post_boundary_history_next]=g_post_boundary_current;
        g_post_boundary_history_next=(g_post_boundary_history_next+1)%nr_post_boundary::kHistory;
    }
    // A close is final. Repeated Presents and later mode/configuration events
    // cannot mutate a historical frame, even when the numeric resource survives.
    g_post_boundary_resource.store(0,std::memory_order_release);
    g_post_boundary_device.store(0,std::memory_order_release);
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
    g_post_boundary_descriptor_resource.store(0,std::memory_order_release);
#endif
    g_post_boundary_current={};
}
bool PostBoundaryWindowCurrentLocked() {
    if(!g_post_boundary_current.frame)return false;
    const auto policy=PostBoundaryProbePolicy();
    const auto &observed=g_post_boundary_current.observation;
    if(g_post_boundary_current.config!=g_config_generation.load(std::memory_order_relaxed) ||
       !g_enabled_fast.load(std::memory_order_relaxed) ||
       (observed.phase && (observed.phase!=policy.phase || observed.probe_mode!=static_cast<unsigned>(policy.mode)))) {
        ClosePostBoundaryLocked(nr_post_boundary::Close::ScopeChanged);return false;
    }
    if(GetTickCount64()-g_post_boundary_current.tick>kPostBoundaryMaxAgeMs) {
        ClosePostBoundaryLocked(nr_post_boundary::Close::Expired);return false;
    }
    return true;
}

struct NrNativeOutputTraceScope;
thread_local NrNativeOutputTraceScope *g_native_output_trace=nullptr;
struct NrNativeOutputTraceScope {
    NrNativeOutputTraceScope *previous=nullptr;
    std::uint64_t resource=0,native_list=0,descriptor_resource=0;
    bool active=false;
    nr_post_boundary::Native evidence;
    NrNativeOutputTraceScope(ID3D12Resource *output,ID3D12GraphicsCommandList *list,unsigned coverage=0) {
        if(!output||!list)return;
        resource=reinterpret_cast<std::uint64_t>(output);
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
        descriptor_resource=NrDescriptorResourceToken(resource);
#endif
        evidence.list=SnapshotPostBoundaryList(list);
        native_list=evidence.list.known?evidence.list.address:reinterpret_cast<std::uint64_t>(list);
        evidence.coverage=coverage;
        previous=g_native_output_trace;g_native_output_trace=this;active=true;
    }
    nr_post_boundary::Native Finish() {
        if(active){g_native_output_trace=previous;active=false;}
        return evidence;
    }
    ~NrNativeOutputTraceScope(){Finish();}
};
void ObserveNativeOutputBarrier(std::uint64_t list,std::uint64_t resource,
        unsigned before,unsigned after) {
    if(g_native_output_trace)
        g_native_output_trace->evidence.Barrier(list==g_native_output_trace->native_list,
            resource==g_native_output_trace->resource,before,after);
}
bool PostBoundaryRecordingActive() {
    return g_native_output_trace || (!g_inside_ngx_evaluate &&
        g_post_boundary_resource.load(std::memory_order_acquire)!=0);
}
void ObservePostBoundaryEvent(nr_post_boundary::Event event,std::uint64_t resource) {
    using namespace nr_post_boundary;
    if(g_native_output_trace) {
        auto &scope=*g_native_output_trace;
        if(resource && resource!=scope.resource)return;
        if(event.list.address!=scope.native_list) {
            if(resource)Increment(scope.evidence.exact_other_lists);
            return;
        }
        if(!resource && event.kind!=Kind::Uav && event.kind!=Kind::Aliasing && event.kind!=Kind::EnhancedGlobal) {
            Increment(scope.evidence.unknown_operations);scope.evidence.coverage|=TargetUnknown;
            if(event.kind==Kind::Opaque)scope.evidence.coverage|=OpaqueOperation;
            return;
        }
        event.order=g_post_boundary_order.fetch_add(1,std::memory_order_relaxed)+1;
        event.target_known=resource!=0;
        if(resource)Increment(scope.evidence.exact_matches);
        else Increment(scope.evidence.exact_global);
        scope.evidence.Append(event);
        return;
    }
    if(g_inside_ngx_evaluate || !g_post_boundary_resource.load(std::memory_order_acquire))return;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(PostBoundaryWindowCurrentLocked() &&
       (resource ? resource==g_post_boundary_resource.load(std::memory_order_relaxed) :
                   g_post_boundary_current.KnowsList(event.list))) {
        event.order=g_post_boundary_order.fetch_add(1,std::memory_order_relaxed)+1;
        event.target_known=resource!=0;
        if(event.source==Source::Native)Increment(g_post_boundary_current.exact_following);
        bool repeated_unknown=false;
        if(!event.target_known) {
            Increment(g_post_boundary_current.unknown_operations);
            for(unsigned i=0;i<g_post_boundary_current.count;++i) {
                const auto &prior=g_post_boundary_current.events[i];
                if(!prior.target_known&&prior.kind==event.kind&&prior.list.address==event.list.address&&
                   prior.list.token==event.list.token&&prior.list.reset==event.list.reset)repeated_unknown=true;
            }
        }
        if(!repeated_unknown)g_post_boundary_current.Append(event);
    }
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
void MarkPostBoundaryObservationGap(unsigned gap) {
    if(g_native_output_trace){g_native_output_trace->evidence.coverage|=gap;return;}
    if(g_inside_ngx_evaluate || !g_post_boundary_resource.load(std::memory_order_acquire))return;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(PostBoundaryWindowCurrentLocked())g_post_boundary_current.coverage|=gap;
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
void ObservePostBoundaryOtherRtv(const nr_post_boundary::List &list,nr_post_boundary::Kind kind) {
    using namespace nr_post_boundary;
    if(g_native_output_trace) {
        if(list.address==g_native_output_trace->native_list) {
            auto &native=g_native_output_trace->evidence;
            if(kind==Kind::ClearRtv)Increment(native.other_rtv_clears);
            else Increment(native.other_rtv_binds);
        }
        return;
    }
    if(g_inside_ngx_evaluate || !g_post_boundary_resource.load(std::memory_order_acquire))return;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(PostBoundaryWindowCurrentLocked() && g_post_boundary_current.KnowsList(list)) {
        if(kind==Kind::ClearRtv)Increment(g_post_boundary_current.other_rtv_clears);
        else Increment(g_post_boundary_current.other_rtv_binds);
    }
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
void RetirePostBoundaryBeforeEvaluate() {
    if(!g_post_boundary_resource.load(std::memory_order_acquire))return;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(PostBoundaryWindowCurrentLocked())ClosePostBoundaryLocked(nr_post_boundary::Close::Superseded);
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
void ObservePostBoundarySubmission(const nr_post_boundary::List &list,std::uint64_t queue,
        std::uint64_t submission,unsigned index=0,unsigned total=0) {
    if(!g_post_boundary_resource.load(std::memory_order_acquire))return;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(PostBoundaryWindowCurrentLocked()) {
        auto &window=g_post_boundary_current;
        const auto same=[&](const nr_post_boundary::List &other) {
            return list.known && other.known && list.token==other.token &&
                list.reset==other.reset && list.device==other.device && list.address==other.address;
        };
        if(same(window.native.list)) {
            if(window.origin_submitted)window.coverage|=nr_post_boundary::SubmissionUnknown;
            else {window.origin_submitted=true;window.origin_queue=queue;window.origin_submission=submission;
                window.origin_submission_index=index;window.origin_submission_count=total;}
        }
        for(unsigned i=0;i<window.native.count;++i)if(same(window.native.events[i].list)) {
            auto &event=window.native.events[i];
            if(event.submitted){window.coverage|=nr_post_boundary::SubmissionUnknown;continue;}
            event.submitted=true;event.queue=queue;event.submission=submission;
            event.submission_index=index;event.submission_count=total;
        }
        for(unsigned i=0;i<window.count;++i)if(same(window.events[i].list)) {
            // Re-execution is observable but not an ordering proof. Keep the
            // first Execute receipt rather than silently replacing its identity.
            if(window.events[i].submitted){window.coverage|=nr_post_boundary::SubmissionUnknown;continue;}
            window.events[i].submitted=true;window.events[i].queue=queue;window.events[i].submission=submission;
            window.events[i].submission_index=index;window.events[i].submission_count=total;
        }
        if(!list.known||!queue)window.coverage|=nr_post_boundary::SubmissionUnknown;
    }
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
std::uint64_t PublishPostBoundary(ID3D12Resource *resource,const nr_post_boundary::Native &native,
        const NrDiagnosticIdentity &identity,unsigned long long feature,std::uint64_t device=0,
        const nr_post_boundary::ObservationScope &observation={},
        nr_post_boundary::Provenance provenance=nr_post_boundary::Provenance::Unknown) {
    if(!resource||!identity.frame_id)return 0;
    if(provenance!=nr_post_boundary::Provenance::Unknown &&
       (!observation || observation.config!=identity.config_generation || observation.feature!=feature))return 0;
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
    const auto descriptor_resource=NrDescriptorResourceToken(reinterpret_cast<std::uint64_t>(resource));
#endif
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(g_post_boundary_current.frame)ClosePostBoundaryLocked(nr_post_boundary::Close::Superseded);
    g_post_boundary_current={};
    g_post_boundary_current.trace=++g_post_boundary_next_trace;
    g_post_boundary_current.frame=identity.frame_id;
    g_post_boundary_current.config=identity.config_generation;
    g_post_boundary_current.feature=feature;
    g_post_boundary_current.observation=observation;
    g_post_boundary_current.provenance=provenance;
    g_post_boundary_current.native=native;
    g_post_boundary_current.device=native.list.device;
    g_post_boundary_current.coverage=native.coverage;
    g_post_boundary_current.tick=GetTickCount64();
    g_post_boundary_device.store(device,std::memory_order_release);
    g_post_boundary_resource.store(reinterpret_cast<std::uint64_t>(resource),std::memory_order_release);
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
    g_post_boundary_current.descriptor_resource=descriptor_resource;
    g_post_boundary_descriptor_resource.store(descriptor_resource,std::memory_order_release);
#endif
    const auto trace=g_post_boundary_current.trace;
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
    return trace;
}
void ForgetPostBoundary(std::uint64_t resource) {
    if(!resource||g_post_boundary_resource.load(std::memory_order_acquire)!=resource)return;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(g_post_boundary_resource.load(std::memory_order_relaxed)==resource){
        ClosePostBoundaryLocked(nr_post_boundary::Close::ResourceRetired);
    }
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
void ForgetPostBoundaryDescriptorResource(std::uint64_t /*address*/,std::uint64_t identity) {
    // The first attached interface may be a shorter-lived proxy. Retirement
    // names the unique descriptor lifetime, not that interface's raw address.
    if(!identity ||
       g_post_boundary_descriptor_resource.load(std::memory_order_acquire)!=identity)return;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(g_post_boundary_current.descriptor_resource==identity &&
       g_post_boundary_descriptor_resource.load(std::memory_order_relaxed)==identity)
        ClosePostBoundaryLocked(nr_post_boundary::Close::ResourceRetired);
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
#endif
void ObserveFollowingOutputBarrier(std::uint64_t resource,unsigned before,unsigned after) {
    if(!resource||g_post_boundary_resource.load(std::memory_order_acquire)!=resource)return;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(PostBoundaryWindowCurrentLocked() && g_post_boundary_resource.load(std::memory_order_relaxed)==resource)
        g_post_boundary_current.Barrier(before,after);
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
void OnPostBoundaryBarrier(reshade::api::command_list *commands,std::uint32_t count,
        const reshade::api::resource *resources,const reshade::api::resource_usage *before,
        const reshade::api::resource_usage *after) {
    if(!resources||!before||!after)return;
    if(g_native_output_trace) {
        const auto native=commands?commands->get_native():0;
        for(std::uint32_t i=0;i<count;++i)
            ObserveNativeOutputBarrier(native,resources[i].handle,
                static_cast<unsigned>(before[i]),static_cast<unsigned>(after[i]));
    } else if(!g_inside_ngx_evaluate && g_post_boundary_resource.load(std::memory_order_acquire)) {
        for(std::uint32_t i=0;i<count;++i)
            ObserveFollowingOutputBarrier(resources[i].handle,
                static_cast<unsigned>(before[i]),static_cast<unsigned>(after[i]));
    }
}
void ObservePostBoundaryCopy(std::uint64_t source,std::uint64_t destination) {
    if(g_inside_ngx_evaluate)return;
    const auto target=g_post_boundary_resource.load(std::memory_order_acquire);
    if(!target||(target!=source&&target!=destination))return;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(PostBoundaryWindowCurrentLocked() && g_post_boundary_resource.load(std::memory_order_relaxed)==target)
        g_post_boundary_current.Copy(source==target,destination==target);
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
void ObservePostBoundaryApiOperation(reshade::api::command_list *commands,nr_post_boundary::Kind kind,
        std::uint64_t resource,bool reads,bool writes) {
    if(g_inside_ngx_evaluate || !g_post_boundary_resource.load(std::memory_order_acquire))return;
    nr_post_boundary::Event event;
    event.list=SnapshotPostBoundaryList(commands?reinterpret_cast<ID3D12GraphicsCommandList *>(commands->get_native()):nullptr);
    event.kind=kind;event.source=nr_post_boundary::Source::ReShade;event.reads=reads;event.writes=writes;
    ObservePostBoundaryEvent(event,resource);
}
bool OnPostBoundaryCopy(reshade::api::command_list *commands,reshade::api::resource source,reshade::api::resource destination) {
    ObservePostBoundaryCopy(source.handle,destination.handle);
    const auto target=g_post_boundary_resource.load(std::memory_order_acquire);
    if(target&&(source.handle==target||destination.handle==target))
        ObservePostBoundaryApiOperation(commands,nr_post_boundary::Kind::Copy,target,source.handle==target,destination.handle==target);
    return false;
}
bool OnPostBoundaryCopyRegion(reshade::api::command_list *commands,reshade::api::resource source,std::uint32_t,
        const reshade::api::subresource_box *,reshade::api::resource destination,std::uint32_t,
        const reshade::api::subresource_box *,reshade::api::filter_mode) {
    ObservePostBoundaryCopy(source.handle,destination.handle);
    const auto target=g_post_boundary_resource.load(std::memory_order_acquire);
    if(target&&(source.handle==target||destination.handle==target))
        ObservePostBoundaryApiOperation(commands,nr_post_boundary::Kind::CopyRegion,target,source.handle==target,destination.handle==target);
    return false;
}
void OnPostBoundaryRenderTargets(reshade::api::command_list *commands,std::uint32_t count,
        const reshade::api::resource_view *views,reshade::api::resource_view) {
    if(g_inside_ngx_evaluate||!commands||!views)return;
    const auto target=g_post_boundary_resource.load(std::memory_order_acquire);
    if(!target)return;
    for(std::uint32_t i=0;i<std::min(count,8u);++i) {
        if(!views[i].handle||commands->get_device()->get_resource_from_view(views[i]).handle!=target)continue;
        AcquireSRWLockExclusive(&g_post_boundary_lock);
        if(PostBoundaryWindowCurrentLocked() && g_post_boundary_resource.load(std::memory_order_relaxed)==target)
            nr_post_boundary::Increment(g_post_boundary_current.render_target_binds);
        ReleaseSRWLockExclusive(&g_post_boundary_lock);
        ObservePostBoundaryApiOperation(commands,nr_post_boundary::Kind::BindTargets,target,false,false);
    }
}
bool OnPostBoundaryClearRenderTarget(reshade::api::command_list *commands,reshade::api::resource_view view,
        const float[4],std::uint32_t,const reshade::api::rect *) {
    if(!g_inside_ngx_evaluate && commands && view.handle && g_post_boundary_resource.load(std::memory_order_acquire))
        ObservePostBoundaryApiOperation(commands,nr_post_boundary::Kind::ClearRtv,
            commands->get_device()->get_resource_from_view(view).handle,false,true);
    return false;
}
void OnPostBoundaryDestroyResource(reshade::api::device *,reshade::api::resource resource) {
    ForgetPostBoundary(resource.handle);
}
void ClosePostBoundaryAtPresent(std::uint64_t device=0) {
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if((!device||device==g_post_boundary_device.load(std::memory_order_relaxed)) && PostBoundaryWindowCurrentLocked())
        ClosePostBoundaryLocked(nr_post_boundary::Close::Present);
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
nr_post_boundary::Window SnapshotPostBoundary(std::uint64_t trace=0,std::uint64_t frame=0,
        unsigned config=0,std::uint64_t feature=0) {
    nr_post_boundary::Window result;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    PostBoundaryWindowCurrentLocked();
    for(unsigned n=0;n<nr_post_boundary::kHistory;++n) {
        const auto &entry=g_post_boundary_history[(g_post_boundary_history_next+nr_post_boundary::kHistory-1-n)%nr_post_boundary::kHistory];
        if(!entry.frame)continue;
        if(trace && (entry.trace!=trace||entry.frame!=frame||entry.config!=config||entry.feature!=feature))continue;
        result=entry;break;
    }
    ReleaseSRWLockExclusive(&g_post_boundary_lock);return result;
}
nr_post_boundary::Selection SelectPostBoundary(const nr_post_boundary::ObservationScope &current,
        const nr_post_boundary::StoreProbeReference &stored) {
    using namespace nr_post_boundary;
    Selection result;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    PostBoundaryWindowCurrentLocked();
    const auto policy=PostBoundaryProbePolicy();
    const bool current_allowed=current && current.config==g_config_generation.load(std::memory_order_relaxed) &&
        g_enabled_fast.load(std::memory_order_relaxed) && current.phase==policy.phase &&
        current.probe_mode==static_cast<unsigned>(policy.mode);
    const Window *paired=nullptr,*latest=nullptr;
    for(unsigned n=0;n<kHistory;++n) {
        const auto &entry=g_post_boundary_history[(g_post_boundary_history_next+kHistory-1-n)%kHistory];
        if(!entry.frame || entry.close==Close::Open)continue;
        if(!paired && Matches(entry,stored))paired=&entry;
        if(!latest && current_allowed && entry.provenance==Provenance::ReadOnlyRecording && Matches(entry,current))latest=&entry;
    }
    result.store_trace_available=paired!=nullptr;
    if(paired && current_allowed && Matches(*paired,current)) {
        result.window=*paired;result.kind=SelectionKind::CurrentStoreProbe;result.paired_with_probe=true;
    } else if(latest) {
        result.window=*latest;result.kind=SelectionKind::CurrentReadOnly;
    } else if(paired) {
        // Retain an explicitly historical exact pair when no current recording
        // exists. Never substitute a nearby/current trace for an evicted pair.
        result.window=*paired;result.kind=SelectionKind::HistoricalStoreProbe;result.paired_with_probe=true;
    }
    if(result.window.trace) {
        const auto &scope=result.window.observation;
        result.phase_current=scope.phase==policy.phase && scope.probe_mode==static_cast<unsigned>(policy.mode);
        auto comparable=scope;comparable.phase=current.phase;comparable.probe_mode=current.probe_mode;
        result.scope_current=current_allowed && comparable==current;
    }
    ReleaseSRWLockExclusive(&g_post_boundary_lock);return result;
}
void RegisterPostBoundaryEvents(bool add) {
    if(add) {
        reshade::register_event<reshade::addon_event::barrier>(OnPostBoundaryBarrier);
        reshade::register_event<reshade::addon_event::copy_resource>(OnPostBoundaryCopy);
        reshade::register_event<reshade::addon_event::copy_texture_region>(OnPostBoundaryCopyRegion);
        reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnPostBoundaryRenderTargets);
        reshade::register_event<reshade::addon_event::clear_render_target_view>(OnPostBoundaryClearRenderTarget);
        reshade::register_event<reshade::addon_event::destroy_resource>(OnPostBoundaryDestroyResource);
    } else {
        AcquireSRWLockExclusive(&g_post_boundary_lock);
        ClosePostBoundaryLocked(nr_post_boundary::Close::Detached);
        ReleaseSRWLockExclusive(&g_post_boundary_lock);
        reshade::unregister_event<reshade::addon_event::barrier>(OnPostBoundaryBarrier);
        reshade::unregister_event<reshade::addon_event::copy_resource>(OnPostBoundaryCopy);
        reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnPostBoundaryCopyRegion);
        reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnPostBoundaryRenderTargets);
        reshade::unregister_event<reshade::addon_event::clear_render_target_view>(OnPostBoundaryClearRenderTarget);
        reshade::unregister_event<reshade::addon_event::destroy_resource>(OnPostBoundaryDestroyResource);
    }
}
