// Included in the generated Core after guide/codec helpers. Calls are serialized
// by g_chain_cs; exact Core recording and queue receipts own every private use.
#define NR_EDIT_PRIVATE_SR_BYPASS_COMPLETE 1
#include "private_sr/nr_private_sr_core_adapter.inl"
namespace nr_post_product {
using Id=std::uint64_t;
using Recording=nr_private_sr::Recording;
using Source=nr_private_sr::Source;
using View=nr_edit_dx12::View;
using State=nr_private_sr_status::State;
struct Flight {
    ID3D12Resource *depth=nullptr,*motion=nullptr;
    nr_private_sr_guides::Input capture{};
    nr_private_sr_guides::Result guides{};
    Recording recording{};
    ID3D12Fence* fence=nullptr;
    Id fence_value=0;
    bool backend_recorded=false,composed=false,submitted=false;
};
struct Owner {
    nr_post_private_sr_dx12::Context* backend=nullptr;
    nr_edit_dx12::Kernels* normalizer=nullptr;
    nr_private_sr_core::Exports exports{};
    nr_private_sr::Shape shape{};
    std::array<Flight,3> flights{};
    Source last_source{},last_capture{};
    LifecycleQueue* queue=nullptr;
    Id bytes=0,configuration=0;
    bool poisoned=false,retiring=false,creation_observed=false;
};
void Say(State s) {
    const auto old=nr_private_sr_status::state.exchange(s);
    if(old!=s)Log("Private residual SR: %s; F8 records admission/submission, not picture acceptance.",nr_private_sr_status::Name(s));
}
void Refuse(State s) {nr_private_sr_status::rejected.fetch_add(1);Say(s);}
bool PrivateCall(){return nr_private_sr_core::InPrivateCall();}
bool SameDevice(ID3D12Device* a,ID3D12Device* b){return IsSameRrDepthDevice(a,b);}
bool RecordingReleased(void*,Recording r) {
    EnterCriticalSection(&g_chain_cs);
    const auto* record=FindLifecycleCommandListLocked(r.lifetime);
    const bool released=!g_lifecycle_tracking_uncertain.load(std::memory_order_acquire)&&
        (!record||(record->detached&&record->in_flight_submissions==0));
    LeaveCriticalSection(&g_chain_cs);return released;
}
Recording RecordingFor(ID3D12GraphicsCommandList* list,LifecycleQueue* queue) {
    if(!list||!queue)return {};
    unsigned long long token=0;
    const auto identity=SnapshotPostBoundaryList(list);
    if(!identity.known||identity.address!=reinterpret_cast<Id>(list)||identity.reset==UINT64_MAX||
       !ReadLifecycleToken(list,token))return {};
    return {reinterpret_cast<Id>(g_codec.device),queue->identity,reinterpret_cast<Id>(list),token,identity.reset+1};
}
void Poison(NrContext& context) {
    if(auto* owner=context.private_sr) {
        owner->poisoned=true;nr_edit_dx12::MarkUncertain(owner->normalizer);
        for(const auto& f:owner->flights)if(f.backend_recorded)nr_post_private_sr_dx12::Discarded(owner->backend,f.recording);
    }
    context.lifecycle_dependencies_quarantined=true;
    Say(State::Quarantined);MarkLifecycleUncertain("private SR uncertain recording or submission");
}
bool Collect(Owner& o) {
    if(o.poisoned||(o.normalizer&&!nr_edit_dx12::Collect(o.normalizer))||!nr_post_private_sr_dx12::Collect(o.backend))return false;
    for(auto& f:o.flights)if(f.recording&&f.submitted&&RecordingReleased(nullptr,f.recording)) {
        const auto done=f.fence?ID3D12Fence_GetCompletedValue(f.fence):UINT64_MAX;
        if(done==UINT64_MAX||done<f.fence_value)continue;
        ReleaseCom(f.fence);
        auto* d=f.depth;auto* m=f.motion;f={};f.depth=d;f.motion=m;
    }
    return true;
}
bool Ready(const NrContext& context) {
    auto* o=context.private_sr;if(!o)return true;
    if(!Collect(*o)||!nr_edit_dx12::ReadyToRetire(o->normalizer)||!nr_post_private_sr_dx12::ReadyToRetire(o->backend))return false;
    for(const auto& f:o->flights)if(f.recording)return false;
    return true;
}
bool Destroy(NrContext& context) {
    auto* o=context.private_sr;if(!o)return true;
    o->retiring=true;nr_post_private_sr_dx12::Cancel(o->backend);
    if(!Ready(context))return false;
    if(!nr_post_private_sr_dx12::Retire(o->backend)||!nr_edit_dx12::DestroyAfterRetirement(o->normalizer)){Poison(context);return false;}
    for(auto& f:o->flights){ReleaseCom(f.fence);ReleaseCom(f.depth);ReleaseCom(f.motion);}
    nr_private_sr_core::ReleaseExports(o->exports);
    nr_private_sr_status::bytes.fetch_sub(o->bytes);nr_private_sr_status::retired.fetch_add(1);
    delete o;context.private_sr=nullptr;return true;
}
void Maintain(NrContext& context) {
    auto* o=context.private_sr;if(!o)return;
    if(context.create_dependencies_quarantined||context.lifecycle_dependencies_quarantined||g_lifecycle_tracking_uncertain.load())return;
    if(!Collect(*o)){Poison(context);return;}
    if(!g_config.enabled||!g_config.private_residual_sr||g_config.temporal_backend||context.retired||
       nr_processing_runtime::Effective()!=nr_processing::Start::After||
       o->configuration!=g_config_generation.load()) {
        o->retiring=true;nr_post_private_sr_dx12::Cancel(o->backend);(void)Destroy(context);
    }
}
void Submitted(NrContext& context,unsigned long long token,LifecycleQueue* queue,unsigned long long value) {
    auto* o=context.private_sr;if(!o)return;
    for(auto& f:o->flights)if(f.recording&&f.recording.lifetime==token) {
        if(f.submitted||o->poisoned||queue!=o->queue||!queue||!queue->fence||!value||queue->identity!=f.recording.queue){Poison(context);return;}
        const nr_private_sr::Fence receipt{queue->identity,reinterpret_cast<Id>(queue->fence),value};
        const bool guides=nr_edit_dx12::Submitted(o->normalizer,f.recording,queue->fence,receipt,true);
        const bool backend=!f.backend_recorded||nr_post_private_sr_dx12::Submitted(o->backend,f.recording,queue->fence,receipt,true);
        if(!guides||!backend){Poison(context);return;}
        f.submitted=true;f.fence=queue->fence;ID3D12Fence_AddRef(f.fence);f.fence_value=value;
        nr_private_sr_status::submitted.fetch_add(1);
        if(f.composed){nr_private_sr_status::adopted.fetch_add(1);Say(State::Submitted);}
    }
}
void Discarded(NrContext& context,unsigned long long token) {
    auto* o=context.private_sr;if(!o)return;
    for(auto& f:o->flights)if(f.recording.lifetime==token&&!f.submitted){
        nr_edit_dx12::Discarded(o->normalizer,f.recording);
        if(f.backend_recorded)nr_post_private_sr_dx12::Discarded(o->backend,f.recording);
        Poison(context);return;
    }
}
bool CreateOwner(NrContext& context,ID3D12Device* device,const NgxHandle* parent,const FeatureRecord& feature,
                 LifecycleQueue* queue,unsigned nw,unsigned nh,unsigned ow,unsigned oh) {
    if(context.private_sr)return false;
    const auto memory=nr_process_memory::Read(ID3D12Device_GetAdapterLuid(device),true);
    if(FAILED(memory.local_result)||memory.local.CurrentUsage>=memory.local.Budget)return false;
    const auto spare=memory.local.Budget-memory.local.CurrentUsage;
    const Id budget=std::min<Id>(768ull<<20,spare>(64ull<<20)?spare-(64ull<<20):0);
    auto* o=new(std::nothrow)Owner;if(!o)return false;
    o->shape={nw,nh,ow,oh,2,2u|(unsigned(feature.create_flags)&8),reinterpret_cast<Id>(device),queue->identity,feature.generation};
    o->queue=queue;o->configuration=g_config_generation.load();
    context.private_sr=o;
    if(!nr_private_sr_core::ResolveExports(o->exports,device,parent,feature.generation)){(void)Destroy(context);return false;}
    auto api=nr_private_sr_core::MakeApi(o->exports);api.recording_released=RecordingReleased;
    auto code=nr_private_sr_artifacts::Kernels();
    o->normalizer=nr_edit_dx12::Create(device,o->shape.device,o->shape.queue,code,SameDevice);
    if(!o->normalizer){(void)Destroy(context);return false;}
    nr_edit_dx12::SetRetirementGuard(o->normalizer,RecordingReleased,nullptr);
    for(auto& f:o->flights)for(unsigned n=0;n<2;++n){
        Id bytes=0;auto*& resource=n?f.motion:f.depth;
        resource=nr_edit_dx12::CreatePlane(device,nw,nh,n?16:41,budget-o->bytes,&bytes);
        o->bytes+=bytes;nr_private_sr_status::Allocated(bytes);
        if(!resource){(void)Destroy(context);return false;}
    }
    Id backend_bytes=0;
    o->backend=nr_post_private_sr_dx12::Create(device,api,o->shape,code,SameDevice,budget-o->bytes,&backend_bytes);
    o->bytes+=backend_bytes;nr_private_sr_status::Allocated(backend_bytes);
    if(!o->backend){(void)Destroy(context);return false;}
    if(nr_post_private_sr_dx12::Poisoned(o->backend)){Poison(context);return false;}
    return true;
}
// Called only after RecordNrUseLocked at the actual pre-native input seam.
// A guide-only recording remains attached even if native SR or NR later fails.
unsigned Capture(ID3D12GraphicsCommandList* list,ID3D12Device* device,NrContext& context,
                 const NgxHandle* parent,const NgxParams* params,const FeatureRecord& feature,
                 const NrGuideBinding& guides,CodecColorMode color,unsigned rw,unsigned rh,unsigned nw,unsigned nh,unsigned ow,unsigned oh) {
    if(!g_config.private_residual_sr)return 0;
    nr_private_sr_status::requested.fetch_add(1);
    if(g_config.temporal_backend){Refuse(State::Conflict);return 0;}
    if(feature.feature_id!=1||g_fg_presentation_policy.Read().late_owned||ID3D12Device_GetNodeCount(device)!=1){Refuse(State::Route);return 0;}
    if(!feature.create_flags_known||(feature.create_flags&1)||
       (color!=CodecColorMode::Automatic&&color!=CodecColorMode::SrgbEncodedSdr)||
       g_config.light_preview||g_config.region_preview||g_config.face_preview||g_config.colour_neutral_probe){Refuse(State::Domain);return 0;}
    CommandListQueueMarker marker{};LifecycleQueue* queue=nullptr;
    if(ReadLastSubmissionQueueMarker(list,marker)&&!marker.flags&&marker.stable_submissions>=kRequiredStableQueueSubmissions)
        for(auto& q:g_lifecycle_queues)if(q.identity==marker.identity&&q.queue&&q.fence){queue=&q;break;}
    if(!queue||ID3D12GraphicsCommandList_GetType(list)!=D3D12_COMMAND_LIST_TYPE_DIRECT){Refuse(State::Queue);return 0;}
    const auto recording=RecordingFor(list,queue);const auto current=nr_source_runtime::CurrentSource();
    if(!recording||!current){Refuse(State::Source);return 0;}
    nr_private_sr_guides::Input input{};input.enabled=true;
    input.source=input.snapshot_source={current.stream,current.epoch,current.sequence};
    input.recording=input.snapshot_recording=recording;input.signature=guides.signature;
    input.native_feature=1;input.native_flags=unsigned(feature.create_flags);input.native_flags_known=feature.create_flags_known;
    input.read_boundary_snapshot=true;input.render_width=rw;input.render_height=rh;
    input.work_width=nw;input.work_height=nh;input.output_width=ow;input.output_height=oh;
    input.original_motion_scale_known=GetFloat(params,"MV.Scale.X",&input.original_motion_scale_x)&&GetFloat(params,"MV.Scale.Y",&input.original_motion_scale_y);
    input.original_jitter_known=GetFloat(params,"Jitter.Offset.X",&input.original_jitter_x)&&GetFloat(params,"Jitter.Offset.Y",&input.original_jitter_y);
    input.reset=true;int reset=1;if(GetInt(params,"Reset",&reset))input.reset=reset!=0;
    // Reference is the actual previous private evaluation; a skipped source
    // deliberately resets rather than reinterpreting one-frame game vectors.
    if(auto* old=context.private_sr)input.motion_reference=old->last_source;
    if(!input.motion_reference||input.motion_reference.stream!=input.source.stream||input.motion_reference.epoch!=input.source.epoch||
       input.motion_reference.sequence+1!=input.source.sequence)input.reset=true;
    unsigned depth_format=guides.signature.depth_format;
    if(depth_format==unsigned(DXGI_FORMAT_R32_TYPELESS))depth_format=unsigned(DXGI_FORMAT_R32_FLOAT);
    if(depth_format==unsigned(DXGI_FORMAT_R16_TYPELESS))depth_format=unsigned(DXGI_FORMAT_R16_UNORM);
    if(depth_format==unsigned(DXGI_FORMAT_R24G8_TYPELESS))depth_format=unsigned(DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
    if(depth_format==unsigned(DXGI_FORMAT_R32G8X24_TYPELESS))depth_format=unsigned(DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
    input.depth={guides.depth,depth_format,0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,true};
    input.motion={guides.motion,guides.signature.motion_format,0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,true};
    if(!nr_source_runtime::PrivateSrInputReadState(list,guides.depth,input.depth.state)||
       !nr_source_runtime::PrivateSrInputReadState(list,guides.motion,input.motion.state)){Refuse(State::Guides);return 0;}
    const auto plan=nr_private_sr_guides::Prepare(input);
    if(!plan){nr_private_sr_status::guide_reason.store(unsigned(plan.reason));Refuse(plan.reason==nr_private_sr_guides::Reason::NonzeroJitter||plan.reason==nr_private_sr_guides::Reason::JitterUnknown?State::Phase:State::Guides);return 0;}
    auto* o=context.private_sr;
    if(o&&(o->retiring||o->shape.render_width!=nw||o->shape.render_height!=nh||o->shape.output_width!=ow||o->shape.output_height!=oh||
           o->shape.queue!=queue->identity||o->shape.create_flags!=(2u|(unsigned(feature.create_flags)&8))||o->configuration!=g_config_generation.load())) {
        if(!Destroy(context)){Refuse(State::Budget);return 0;}o=nullptr;
    }
    if(!o){if(!CreateOwner(context,device,parent,feature,queue,nw,nh,ow,oh)){if(!context.lifecycle_dependencies_quarantined)Refuse(State::Budget);return 0;}o=context.private_sr;}
    if(!Collect(*o)){Poison(context);return 0;}
    if(o->last_capture.stream==input.source.stream&&o->last_capture.epoch==input.source.epoch&&
       o->last_capture.sequence>=input.source.sequence){Refuse(State::Duplicate);return 0;}
    Flight* selected=nullptr;unsigned index=0;
    for(unsigned n=0;n<o->flights.size();++n){auto& f=o->flights[n];
        if(f.recording&&(!f.submitted||f.recording==recording)){Refuse(State::Busy);return 0;}
        if(f.recording&&f.capture.source==input.source){Refuse(State::Duplicate);return 0;}
        if(!f.recording&&!selected){selected=&f;index=n;}}
    if(!selected){Refuse(State::Busy);return 0;}
    auto& f=*selected;f.capture=input;
    const View depth{f.depth,41,0,D3D12_RESOURCE_STATE_COMMON,true},motion{f.motion,16,0,D3D12_RESOURCE_STATE_COMMON,true};
    f.guides=nr_private_sr_guides::Record(o->normalizer,list,input,depth,motion);
    if(!f.guides.dispatch.recorded){nr_private_sr_status::guide_reason.store(unsigned(f.guides.reason));Refuse(State::Guides);return 0;}
    f.recording=recording;o->last_capture=input.source;nr_private_sr_status::captures.fetch_add(1);return index+1;
}
bool Finish(ID3D12GraphicsCommandList* list,NrContext& context,NrSlot& slot,const FeatureRecord& feature,
            unsigned capture,unsigned ow,unsigned oh,unsigned x,unsigned y) {
    if(!capture)return true;auto* o=context.private_sr;
    if(!o||capture>o->flights.size()){Poison(context);return false;}
    auto& f=o->flights[capture-1];const auto current=nr_source_runtime::CurrentSource();
    const Source source{current.stream,current.epoch,current.sequence};
    const auto recording=RecordingFor(list,o->queue);
    if(!source||!(source==f.capture.source)||!(recording==f.recording)){Refuse(State::Source);return true;}
    if(feature.feature_id!=1||!slot.snapshots_native_output||slot.result_format!=DXGI_FORMAT_R16G16B16A16_FLOAT||
       slot.result_uav_format!=DXGI_FORMAT_R16G16B16A16_FLOAT){Refuse(State::Domain);return true;}
    // This entry follows successful native Evaluate, the exact B snapshot,
    // every NR layer and the final controlled combine on this same recording.
    const nr_private_sr_guides::FinishProof proof{source,source,source,recording,1,kNgxSuccess,true};
    const auto normalized=nr_private_sr_guides::Finish(f.capture,f.guides,proof);
    if(!normalized.post_sr_phase_verified){Refuse(State::Phase);return true;}
    nr_post_private_sr_dx12::Input input{};
    input.pair.base={slot.source,10,0,unsigned(slot.source_state),true};
    input.pair.candidate={slot.result,10,0,unsigned(slot.result_state),true};
    input.pair.base_rect={0,0,ow,oh};input.pair.candidate_rect={x,y,ow,oh};
    input.pair.base_source=input.pair.candidate_source=source;
    input.pair.domain=nr_post_residual_dx12::Domain::SdrEncodedFp16;input.pair.same_encoding_verified=true;
    input.pair.work_width=o->shape.render_width;input.pair.work_height=o->shape.render_height;
    input.target=input.pair.candidate;input.target_rect=input.pair.candidate_rect;input.target_contains_candidate=true;
    input.depth={f.depth,41,0,D3D12_RESOURCE_STATE_COMMON,true};input.motion={f.motion,16,0,D3D12_RESOURCE_STATE_COMMON,true};
    input.recording=recording;input.guide_source=normalized.source;input.motion_reference=normalized.motion_reference;
    input.view=context.instance_id;input.configuration=Id(g_config_generation.load())+1;
    input.encoding_generation=Id(g_history_generation.load())+1;input.reset=f.capture.reset;
    input.ordered_queue_verified=true;input.post_sr_phase_verified=normalized.post_sr_phase_verified;
    input.normalized_guide_units_verified=normalized.normalized_guide_units_verified;input.native_result=kNgxSuccess;
    const auto result=nr_post_private_sr_dx12::Record(o->backend,list,input);
    f.backend_recorded=result.recorded;f.composed=result.composed;
    if(result.recorded)nr_private_sr_status::recorded.fetch_add(1);
    if(result.composed){o->last_source=source;nr_private_sr_status::composed.fetch_add(1);Say(State::Recorded);}
    else if(result.status==nr_post_private_sr_dx12::Status::WaitingForSr){
        if(!o->creation_observed){o->creation_observed=true;nr_private_sr_status::creations.fetch_add(1);}Say(State::Creating);}
    else if(result.status==nr_post_private_sr_dx12::Status::Poisoned){Poison(context);return false;}
    else Refuse(result.status==nr_post_private_sr_dx12::Status::Busy?State::Busy:State::Guides);
    return true;
}
}
