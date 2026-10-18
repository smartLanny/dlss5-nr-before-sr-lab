// Included inside nr_compat_present_runtime after models and before events.
// This is a private Present observation capture, never a native SR-stage F7.
namespace nr_present_capture {
std::atomic<unsigned long long> requested{0};
std::atomic<unsigned> status{0}; // idle, pending, exported, failed
std::atomic<unsigned> remaining_exports{0};
unsigned long long consumed=0,next_token=1;
struct Plane {
    nr_replay::Recorder recorder;
    unsigned index=nr_replay::Recorder::Invalid;
    nr_dual_capture::Identity identity{};
};
Plane planes[kPassLimit]{};
struct Active {
    unsigned long long request=0,source=0,generation=0,token=0;
    Flight* flight=nullptr;unsigned count=0;
    bool finished=false,submitted=false;
    ID3D12Fence* fence=nullptr;
};
Active active{};
void Fail(const char* stage){status.store(3);Log("[present-capture] failed stage=%s request=%llu generation=%llu source=%llu",stage,active.request,active.generation,active.source);}
bool RequestCapture() {
    auto old=requested.load(std::memory_order_relaxed);
    while(old!=UINT64_MAX&&!requested.compare_exchange_weak(old,old+1)){}
    if(old==UINT64_MAX)return false;
    status.store(1,std::memory_order_release);
    return true;
}
bool Empty() {for(auto& p:planes)if(!p.recorder.Empty())return false;return true;}
void Collect() {
    for(auto& p:planes)p.recorder.Collect(
        [](void* key,std::uint64_t value){auto* fence=static_cast<ID3D12Fence*>(key);
            if(!fence||!value)return false;
            const auto done=ID3D12Fence_GetCompletedValue(fence);return done!=UINT64_MAX&&done>=value;
        },[&](nr_replay::ExportJob* frame) {
            const bool complete=frame->header.completion==static_cast<unsigned>(nr_replay::Completion::Complete);
            if(!complete)status.store(3);
            nr_dual_capture::HandoffWithIdentity(frame,p.identity);
        });
    if(Empty()&&active.token){ReleaseCom(active.fence);active={};}
}
bool BeginCapture(Context& c,Flight& f,unsigned long long source) {
    Collect();const auto request=requested.load(std::memory_order_acquire);
    if(!request||request==consumed||!source||!g_config.enabled||
        c.timeline.Poisoned()||!Empty()||active.token||nr_dual_capture::workers.load())return false;
    // Incomplete layer creation/warmup is not a full comparison frame.
    for(unsigned i=0;i<c.passes;++i)if(!c.layers[i].context.nr_handle||
        c.layers[i].context.evaluations<=g_config.warmup_after_create)return false;
    if(!next_token||next_token==UINT64_MAX){status.store(3);return false;}
    active={request,source,c.generation,next_token++,&f,c.passes,false,false,nullptr};
    status.store(1);remaining_exports.store(c.passes);return true;
}
void LayerBegin(Context& c,Flight& f,unsigned layer) {
    if(!active.token||active.flight!=&f||active.generation!=c.generation||layer>=active.count)return;
    auto& p=planes[layer];auto& model=c.layers[layer];
    const auto flight=static_cast<unsigned>(&f-c.flights);
    if(flight>=kFlightCount)return;
    const auto* parameters=model.evaluation_params[flight];
    ID3D12Resource *depth=nullptr,*motion=nullptr;
    if(!GetResource(parameters,"DLSSNR.Depth",&depth)||!GetResource(parameters,"DLSSNR.MVec",&motion)||!depth||!motion){Fail("bound-guides");return;}
    nr_replay::Header h{};h.sequence=(active.request<<8)|layer;
    h.instance=model.context.instance_id;h.mode=model.context.mode;h.config=g_config_generation.load();h.frame=active.source;
    h.source_width=c.source.width;h.source_height=c.source.height;
    h.work_width=model.context.width;h.work_height=model.context.height;
    h.color_mode=static_cast<unsigned>(model.context.codec_color_mode);h.source_dirty=kSourceDirty?1:0;
    std::snprintf(h.source_commit,sizeof(h.source_commit),"%s",kSourceCommit);
    std::snprintf(h.build,sizeof(h.build),"%s",kBuildIdentity);nr_replay::SnapshotScalars(h,parameters);
    auto& binding=layer==0?c.image:model.binding;
    nr_replay::Input inputs[nr_replay::kPlanes]={nr_replay::Texture(c.image.source,4),
        nr_replay::Texture(binding.proxy,4),nr_replay::Texture(binding.neural,4),
        nr_replay::Texture(depth,1),nr_replay::Texture(motion,2),{},
        nr_replay::Texture(c.image.result,4,c.image.result_format)};
    if(!p.recorder.Ensure(c.device)){Fail("recorder-pipeline");return;}
    p.index=p.recorder.Arm(h,inputs,IsSameRrDepthDevice);
    if(p.index==nr_replay::Recorder::Invalid){Fail("arm-header-or-resources");return;}
    if(!p.recorder.Begin(f.list,p.index,active.token)){p.recorder.Discard(p.index);p.index=nr_replay::Recorder::Invalid;Fail("recording-ledger");return;}
    p.identity={active.request,c.generation,model.context.instance_id,active.source,
        layer?c.numerator:1u,layer?c.denominator:1u,static_cast<unsigned>(model.context.codec_color_mode),g_config.generic_residual?1u:0u};
    p.identity.generic_algorithm=g_config.generic_algorithm;
    std::copy_n(g_config.generic_gains,5,p.identity.gains);
    p.identity.full_passes=FullReference(g_config)?c.passes:0;p.identity.layer=layer+1;
    p.identity.present=true;p.identity.export_status=&status;p.identity.remaining=&remaining_exports;
}
void Finish(Context& c,Flight& f) {
    if(!active.token||active.flight!=&f||active.generation!=c.generation)return;
    const auto previous=c.image.result_state;
    Transition(f.list,c.image.result,c.image.result_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    bool complete=true;
    for(unsigned i=0;i<active.count;++i) {
        auto& p=planes[i];auto& model=c.layers[i];
        if(p.index==nr_replay::Recorder::Invalid){complete=false;continue;}
        nr_replay::Header h{};h.evaluation=model.context.evaluations;
        h.since_reset=model.context.successful_evaluations_since_reset;h.frame=active.source;
        complete&=p.recorder.Finish(f.list,p.index,h);p.index=nr_replay::Recorder::Invalid;
    }
    Transition(f.list,c.image.result,c.image.result_state,previous);
    active.finished=complete;consumed=active.request;if(!complete)Fail("finish-incomplete");
}
// Call only after the private Execute + Signal succeeded. These command lists
// are owned by the flight ring and will never be replayed before allocator reset.
void Submit(unsigned long long source,ID3D12Fence* fence,unsigned long long value) {
    if(!active.token||active.source!=source||!fence||!value||active.submitted)return;
    active.fence=fence;ID3D12Fence_AddRef(fence);active.submitted=true;
    for(auto& p:planes){p.recorder.Submit(active.token,fence,value);p.recorder.Invalidate(active.token,false);}
}
// Before releasing the context, collect exact completed submissions. Unknown
// or opaque work retains its capture resources alongside the private context.
bool Retire(Context& c) {
    if(active.token&&active.generation==c.generation&&!active.submitted)return false;
    Collect();if(!Empty()||active.token)return false;
    for(auto& p:planes)if(!p.recorder.ResetDevice())return false;
    return true;
}
}
