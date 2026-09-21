// Inside the existing Core namespace, after helpers and before generated RR/SR
// dispatch. No second graphics hook or Present observer is installed here.
#include "nr_private_sr_core_adapter.inl"
namespace nr_edit_core {
using Id=std::uint64_t;
using Recording=nr_private_sr::Recording;
using Source=nr_private_sr::Source;
using View=nr_edit_dx12::View;
using Mode=nr_edit_policy::Mode;
constexpr unsigned kEntries=8,kReceipts=64;
enum class State:unsigned {Off,NativeFallback,PreparingImage,ImageWarmup,Prepared,
    PrivateSrWarmup,DeferredRecorded,RrSpatialRecorded,RrHistoryRecorded,Busy,Poisoned};
std::atomic<State> state{State::Off};
std::atomic<Id> image_ready{0},composed{0},submitted{0},completed{0},allocated_bytes{0};
std::atomic<unsigned> reported_r_width{0},reported_r_height{0},reported_o_width{0},reported_o_height{0};
std::atomic<unsigned> rr_history_status{0};
std::atomic<const char*> reason{"off"};
void Say(State s,const char* why) {
    const auto old=state.exchange(s);const auto* previous=reason.exchange(why);
    if(old!=s||std::strcmp(previous,why)!=0)
        Log("[edit-experiment] state=%u reason=%s; composed=recorded, not GPU/picture proof.",static_cast<unsigned>(s),why);
}
bool PrivateSrCall() noexcept{return nr_private_sr_core::InPrivateCall();}
struct GuideConstants {
    unsigned out[2]{},depth[2]{},motion[2]{},depth_origin[2]{},motion_origin[2]{};
    float motion_scale[2]{},projection_a=0,projection_b=0;
    unsigned linear_depth=0,inverted=0;
};
static_assert(sizeof(GuideConstants)==64);
struct Receipt {bool recorded=false,backend_recorded=false;Recording recording{};nr_private_sr::Fence fence{};bool submitted=false,visible=false;};
struct Entry {
    bool occupied=false,retiring=false,releasing=false,poisoned=false;
    unsigned kind=0;const NgxHandle* handle=nullptr;
    Id feature=0,configuration=0,bytes=0,view=0;
    nr_private_sr::Shape shape{};
    ID3D12Device* device=nullptr;LifecycleQueue* queue=nullptr;
    nr_edit_dx12::Kernels* guide_kernels=nullptr;
    ID3D12Resource *depth=nullptr,*motion=nullptr;
    nr_deferred_dx12::Context* deferred=nullptr;
    nr_rr_dx12::Context* rr=nullptr;
    nr_private_sr_core::Exports exports{};
    std::array<Receipt,kReceipts> receipts{};
    nr_edit_policy::Camera previous_camera{};
    unsigned long long primary_instance=0,primary_history=0;
    Source previous_source{};bool previous_jitter_zero=false,previous_projection_known=false;float previous_projection[2]{};
};
Entry entries[kEntries];
std::atomic<bool> retirement_pending{false};
bool RecordingReleased(void*,Recording recording) {
    if(g_lifecycle_tracking_uncertain.load(std::memory_order_acquire))return false;
    auto* r=FindLifecycleCommandListLocked(recording.lifetime);
    return !r||(r->detached&&r->in_flight_submissions==0);
}
void Poison(Entry& e) {
    if(e.releasing)return;
    e.poisoned=true;e.previous_camera={};
    nr_edit_dx12::MarkUncertain(e.guide_kernels);
    nr_deferred_dx12::MarkUncertain(e.deferred);nr_rr_dx12::MarkUncertain(e.rr);
    Say(State::Poisoned,"opaque-or-lifecycle-failure; restart-required");
}
bool ReserveReceipt(Entry& e,Recording recording) {
    for(auto& p:e.receipts)if(p.recording==recording)return !p.submitted;
    for(auto& p:e.receipts)if(!p.recording){p.recording=recording;return true;}
    return false;
}
void Visible(Entry& e,Recording r) {for(auto& p:e.receipts)if(p.recording==r){p.visible=true;return;}}
void SubmitLocked(unsigned long long token,LifecycleQueue* queue,unsigned long long value) {
    if(!queue||!queue->fence||!value)return;
    for(auto& e:entries)if(e.occupied&&!e.releasing)for(auto& p:e.receipts)if(p.recorded&&p.recording.lifetime==token) {
        if(p.submitted||e.poisoned||p.recording.queue!=queue->identity||e.queue!=queue){Poison(e);continue;}
        nr_private_sr::Fence receipt{queue->identity,reinterpret_cast<Id>(queue->fence),value};
        const bool a=nr_edit_dx12::Submitted(e.guide_kernels,p.recording,queue->fence,receipt,true);
        bool b=true;
        if(p.backend_recorded){
            b=e.deferred?nr_deferred_dx12::Submitted(e.deferred,p.recording,queue->fence,receipt,true):
                nr_rr_dx12::Submitted(e.rr,p.recording,queue->fence,receipt,true);
        }
        if(!a||!b||nr_deferred_dx12::IsPoisoned(e.deferred)||nr_rr_dx12::IsPoisoned(e.rr)){
            Poison(e);MarkLifecycleUncertain("edit submission lost exact backend receipt");continue;
        }
        p.submitted=true;p.fence=receipt;if(p.visible)submitted.fetch_add(1);
    }
}
void InvalidateLocked(unsigned long long token,bool discarded) {
    for(auto& e:entries)if(e.occupied&&!e.releasing)for(auto& p:e.receipts)if(p.recording.lifetime==token&&discarded)Poison(e);
}
// Opaque vendor destruction outside the chain lock. Entry addresses remain
// pinned throughout release; no live source may access a releasing entry.
void DrainRetirements(){
    if(!retirement_pending.exchange(false,std::memory_order_acq_rel))return;
    for(auto& e:entries){
        EnterCriticalSection(&g_chain_cs);
        if(!e.occupied||!e.retiring||e.releasing||e.poisoned||g_lifecycle_tracking_uncertain.load()){
            LeaveCriticalSection(&g_chain_cs);continue;
        }
        bool receipts=false;for(const auto& p:e.receipts)receipts|=p.recorded;
        const bool ready=!receipts&&nr_deferred_dx12::ReadyToRetire(e.deferred)&&
            nr_rr_dx12::ReadyToRetire(e.rr)&&nr_edit_dx12::ReadyToRetire(e.guide_kernels);
        if(!ready){retirement_pending.store(true);LeaveCriticalSection(&g_chain_cs);continue;}
        e.releasing=true;LeaveCriticalSection(&g_chain_cs);
        const bool released=nr_deferred_dx12::Retire(e.deferred)&&nr_rr_dx12::Retire(e.rr)&&
            nr_edit_dx12::DestroyAfterRetirement(e.guide_kernels);
        if(released){ReleaseCom(e.depth);ReleaseCom(e.motion);ReleaseCom(e.device);
            nr_private_sr_core::ReleaseExports(e.exports);}
        EnterCriticalSection(&g_chain_cs);
        if(released){g_dual_allocated_bytes.fetch_sub(e.bytes);allocated_bytes.fetch_sub(e.bytes);e={};}
        else{e.releasing=false;Poison(e);MarkLifecycleUncertain("private edit release failed outside lifecycle locks");}
        LeaveCriticalSection(&g_chain_cs);
    }
}
void CollectLocked() {
    for(auto& e:entries)if(e.occupied&&!e.releasing) {
        if(g_lifecycle_tracking_uncertain.load(std::memory_order_acquire)){Poison(e);continue;}
        if(e.poisoned)continue;
        for(auto& p:e.receipts)if(p.submitted&&e.queue&&e.queue->fence){
            const auto value=ID3D12Fence_GetCompletedValue(e.queue->fence);
            if(value==UINT64_MAX){Poison(e);break;}
            if(value>=p.fence.value&&RecordingReleased(nullptr,p.recording)){
                if(p.visible)completed.fetch_add(1);p={};
            }
        }
        const auto f=FindFeature(e.handle);
        if(!g_config.enabled||!nr_edit_policy::Selected(g_config.edit_experiment,e.kind)||
           e.configuration!=g_config_generation.load(std::memory_order_acquire)||
           f.handle!=e.handle||f.generation!=e.feature||f.retiring||f.pending)e.retiring=true;
        if(!e.retiring)continue;
        nr_deferred_dx12::Cancel(e.deferred);nr_rr_dx12::Cancel(e.rr);
        retirement_pending.store(true,std::memory_order_release);
    }
}
LifecycleQueue* ProvenQueue(ID3D12GraphicsCommandList* list) {
    CommandListQueueMarker marker{};
    if(!ReadLastSubmissionQueueMarker(list,marker)||marker.flags||marker.stable_submissions<kRequiredStableQueueSubmissions)return nullptr;
    for(auto& g:g_graphics_queues)if(g.live&&g.identity==marker.identity&&g.queue){
        const auto d=ID3D12CommandQueue_GetDesc(g.queue);
        if(d.Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return nullptr;
        return FindOrCreateLifecycleQueueLocked(g.queue);
    }
    return nullptr;
}
bool StateView(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,unsigned format,View& out) {
    out={};if(!list||!resource||!format)return false;
    nr_source_runtime::Lock lock;
    const auto recording=nr_source_runtime::RecordingOf(list);
    const auto identity=nr_source_runtime::ResourceOf(resource);
    unsigned value=0;if(!recording||!identity||!nr_source_runtime::StateOf(recording,identity,value))return false;
    out={resource,format,0,value,true};return true;
}
bool ExactFp16(ID3D12Resource* r,unsigned w,unsigned h) {
    if(!r||!w||!h)return false;const auto d=ID3D12Resource_GetDesc(r);
    return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==w&&d.Height==h&&
        d.MipLevels==1&&d.DepthOrArraySize==1&&d.SampleDesc.Count==1&&
        (d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||d.Format==DXGI_FORMAT_R16G16B16A16_TYPELESS);
}
struct Preflight {
    ID3D12Resource *color=nullptr,*output=nullptr;
    unsigned rw=0,rh=0,ow=0,oh=0;View color_view{},output_view{};
    LifecycleQueue* queue=nullptr;
};
bool PreflightInput(ID3D12GraphicsCommandList* list,const NgxParams* params,const FeatureRecord& f,Preflight& p) {
    if(!params||!list||!f.generation||!f.create_flags_known||(f.create_flags&1)==0)return false;
    // Private SR follows the proven native quality/extent contract, not an
    // unrelated hard-coded quality preset at every game's current R/O.
    if(f.feature_id==1&&(!f.create_contract_known||f.perf_quality<0||f.perf_quality>5))return false;
    unsigned x=0,y=0,ox=0,oy=0;
    GetUInt(params,"DLSS.Input.Color.Subrect.Base.X",&x);GetUInt(params,"DLSS.Input.Color.Subrect.Base.Y",&y);
    GetUInt(params,"DLSS.Output.Subrect.Base.X",&ox);GetUInt(params,"DLSS.Output.Subrect.Base.Y",&oy);
    if(x||y||ox||oy||!GetResource(params,"Color",&p.color)||!GetResource(params,"Output",&p.output)||p.color==p.output)return false;
    if(!ResolveRenderExtentFromEvaluate(params,f,p.color,0,0,p.rw,p.rh)||
       !ResolveOutputExtentFromEvaluate(params,f,p.output,0,0,p.rw,p.rh,p.ow,p.oh)||
       !ExactFp16(p.color,p.rw,p.rh)||!ExactFp16(p.output,p.ow,p.oh))return false;
    const auto color=ReadColorContract(params);NrColorSemanticContract semantic{};
    semantic.exposure=color;semantic.flags_known=f.create_flags_known;
    const auto read=NrResolveColorViewFromList(list,p.color,ID3D12Resource_GetDesc(p.color),f.generation,false,f.create_flags,&semantic);
    const auto write=NrResolveColorViewFromList(list,p.output,ID3D12Resource_GetDesc(p.output),f.generation,true,f.create_flags,&semantic);
    if(!read||!write||read.typed.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT||write.typed.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return false;
    if(!std::isfinite(color.pre_exposure)||color.pre_exposure<=1e-6f||!std::isfinite(color.exposure_scale)||color.exposure_scale<=1e-6f)return false;
    p.queue=ProvenQueue(list);
    return p.queue&&StateView(list,p.color,10,p.color_view)&&StateView(list,p.output,10,p.output_view);
}
bool ReadMatrix(const NgxParams* p,const char* key,float (&m)[16]) {
    void* data=nullptr;SIZE_T bytes=0;const auto get=ParamSlot<GetVoidFn>(p,8);
    if(!get||get(p,key,&data)!=kNgxSuccess||!data||
       !ReadProcessMemory(GetCurrentProcess(),data,m,sizeof(m),&bytes)||bytes!=sizeof(m))return false;
    for(float v:m)if(!std::isfinite(v))return false;return true;
}
nr_edit_policy::Camera ReadCamera(const NgxParams* params,bool jitter_zero) {
    nr_edit_policy::Camera c{};if(!jitter_zero)return c;
    float p[16]{},v[16]{};nr_rr_depth::Projection depth{};
    if(!ReadMatrix(params,"ViewToClipMatrix",p)||!ReadMatrix(params,"WorldToViewMatrix",v)||!nr_rr_depth::Resolve(p,depth))return c;
    c.world_to_clip=nr_edit_policy::Multiply(nr_edit_policy::ColumnMajor(p),nr_edit_policy::ColumnMajor(v));
    nr_edit_policy::Matrix inverse{};c.verified=nr_edit_policy::Invert(c.world_to_clip,inverse);return c;
}
Entry* AcquireEntry(const FeatureRecord& f,const NgxHandle* handle,ID3D12Device* device,LifecycleQueue* queue,
                    const nr_private_sr::Shape& shape) {
    const auto cfg=g_config_generation.load(std::memory_order_acquire);
    CollectLocked();Entry* empty=nullptr;
    for(auto& e:entries){if(!e.occupied&&!empty)empty=&e;
        if(e.occupied&&!e.retiring&&e.handle==handle&&e.feature==f.generation){
            if(e.configuration==cfg&&e.shape==shape&&!e.poisoned)return &e;e.retiring=true;
        }}
    if(!empty)return nullptr;
    auto& e=*empty;e.occupied=true;e.kind=f.feature_id;e.handle=handle;e.feature=f.generation;
    e.configuration=cfg;e.shape=shape;e.queue=queue;e.device=device;ID3D12Device_AddRef(device);
    e.view=reinterpret_cast<Id>(handle);
    const auto used=g_dual_allocated_bytes.load();
    const auto limit=std::uint64_t(g_config.edit_experiment.budget_mib)*1024*1024;
    const auto own=allocated_bytes.load();
    const auto budget=std::min<std::uint64_t>(used<kDualTotalBudget?kDualTotalBudget-used:0,own<limit?limit-own:0);
    auto charge=[&](std::uint64_t n){e.bytes+=n;g_dual_allocated_bytes.fetch_add(n);allocated_bytes.fetch_add(n);};
    std::array<nr_edit_dx12::Bytecode,static_cast<unsigned>(nr_edit_dx12::Kernel::Count)> guides{};
    guides[static_cast<unsigned>(nr_edit_dx12::Kernel::NormalizeGuides)]=nr_edit_binaries::Normalize();
    e.guide_kernels=nr_edit_dx12::Create(device,shape.device,shape.queue,guides,IsSameRrDepthDevice);
    if(!e.guide_kernels){e.retiring=true;retirement_pending.store(true);return nullptr;}
    nr_edit_dx12::SetRetirementGuard(e.guide_kernels,RecordingReleased,nullptr);
    std::uint64_t bytes=0;
    e.depth=nr_edit_dx12::CreatePlane(device,shape.render_width,shape.render_height,41,budget,&bytes);charge(bytes);
    e.motion=nr_edit_dx12::CreatePlane(device,shape.render_width,shape.render_height,16,budget-e.bytes,&bytes);charge(bytes);
    if(!e.depth||!e.motion){nr_edit_dx12::DestroyAfterRetirement(e.guide_kernels);e.retiring=true;retirement_pending.store(true);return nullptr;}
    if(f.feature_id==1){
        if(!nr_private_sr_core::ResolveExports(e.exports,device,handle,f.generation)){nr_edit_dx12::DestroyAfterRetirement(e.guide_kernels);e.retiring=true;retirement_pending.store(true);return nullptr;}
        auto api=nr_private_sr_core::MakeApi(e.exports);api.recording_released=RecordingReleased;
        e.deferred=nr_deferred_dx12::Create(device,api,shape,nr_edit_binaries::ModelKernels(),IsSameRrDepthDevice,budget-e.bytes,&bytes);
        charge(bytes);nr_deferred_dx12::SetRetirementGuard(e.deferred,RecordingReleased,nullptr);
    }else{
        e.rr=nr_rr_dx12::Create(device,shape,nr_edit_binaries::ModelKernels(),IsSameRrDepthDevice,budget-e.bytes,&bytes);
        charge(bytes);nr_rr_dx12::SetRetirementGuard(e.rr,RecordingReleased,nullptr);
    }
    if(!e.deferred&&!e.rr){nr_edit_dx12::DestroyAfterRetirement(e.guide_kernels);e.retiring=true;retirement_pending.store(true);return nullptr;}
    return &e;
}
class Transaction {
    EvaluateFeatureFn original_;ID3D12GraphicsCommandList* native_list_;const NgxHandle* handle_;
    const NgxParams* params_;void* callback_;FeatureRecord feature_;Preflight input_;
    std::optional<NativeCommandStateScope> before_,after_;
    Substitution image_{};Entry* entry_=nullptr;Recording recording_{};Source source_{};
    nr_deferred_dx12::Input deferred_{};nr_rr_dx12::Input rr_{};
    LifecycleCommandList* lifecycle_=nullptr;bool created_record_=false,own_recorded_=false;
    nr_edit_policy::Camera camera_{};bool jitter_zero_=false,projection_known_=false;float projection_[2]{};
    ID3D12GraphicsCommandList* List(){return before_?before_->command_list:native_list_;}
    static View Own(ID3D12Resource* p,unsigned format){return {p,format,0,D3D12_RESOURCE_STATE_COMMON,true};}
public:
    Transaction(EvaluateFeatureFn original,ID3D12GraphicsCommandList* list,const NgxHandle* h,const NgxParams* p,void* cb,
        const FeatureRecord& f,Preflight input):original_(original),native_list_(list),handle_(h),params_(p),callback_(cb),feature_(f),input_(input){}
    ~Transaction(){if(entry_)for(auto& p:entry_->receipts)if(p.recording==recording_&&!p.recorded)p={};
        if(lifecycle_)RollBackEmptyLifecycleCommandListLocked(List(),lifecycle_,created_record_);}
    bool Eligible(){return g_config.enabled&&!g_config.identity_use_original&&!OtherNrOwnerActive()&&
        !g_lifecycle_tracking_uncertain.load()&&!g_feature_tracking_uncertain.load()&&nr_compat_present_runtime::InlineDrainReady();}
    bool CaptureBeforeState(){before_.emplace(native_list_);return before_->active;}
    nr_edit_flow::Step ProducePrivateImage(){
        using P=nr_edit_flow::Phase;Say(State::PreparingImage,"private-E_R");
        lifecycle_=FindOrCreateLifecycleCommandListLocked(List(),created_record_);if(!lifecycle_)return {};
        const auto s=nr_source_runtime::CurrentSource();if(!s)return {};
        source_={s.stream,s.epoch,s.sequence};
        NrPrivateImageRequest request{};request.kind=static_cast<NrPrivateImageKind>(feature_.feature_id);
        request.native_handle=handle_;request.feature_generation=feature_.generation;
        request.source_serial=nr_source_runtime::CurrentRetryToken();request.recording_token=lifecycle_->token;
        request.configuration=g_config_generation.load();request.selected=true;
        {ImageScope scope;image_=PrepareNrPrivateOrBefore(List(),handle_,params_,feature_,&request);}
        if(image_.fatal_recording)return {P::Fatal,image_.fatal_recording,true};
        if(g_lifecycle_tracking_uncertain.load()||g_feature_tracking_uncertain.load())return {P::Fatal,0xBAD00005u,true};
        if(!image_.private_image.ready){Say(State::ImageWarmup,"NR-warmup-or-input-decline; native-kept");return {P::Declined,0,(nr_source_runtime::evaluation&&nr_source_runtime::evaluation->private_recorded)};}
        image_ready.fetch_add(1);return {P::Ready,0,true};
    }
    nr_edit_flow::Step PrepareEdit(){
        using P=nr_edit_flow::Phase;const auto& img=image_.private_image;const auto& g=img.guides.signature;
        ID3D12Device* device=nullptr;
        if(FAILED(ID3D12GraphicsCommandList_GetDevice(List(),IID_ID3D12Device,reinterpret_cast<void**>(&device)))||!device)return {};
        const auto device_id=NrFormatDeviceIdentity(device);
        unsigned long long token=0;bool valid=ReadLifecycleToken(List(),token)&&token==img.recording_token;
        auto* record=valid?FindLifecycleCommandListLocked(token):nullptr;
        nr_source_frame::Recording source_record{};{
            nr_source_runtime::Lock lock;source_record=nr_source_runtime::RecordingOf(List());
        }
        valid=valid&&record&&record->queue_proof==RecordingQueueProof::StableHistory&&
            record->expected_queue_identity==input_.queue->identity&&source_record&&
            img.original==input_.color&&img.render_width==input_.rw&&img.render_height==input_.rh&&
            img.origin_x==0&&img.origin_y==0&&ExactFp16(img.edited,input_.rw,input_.rh);
        float jx=0,jy=0,mx=0,my=0;
        valid=valid&&GetFloat(params_,"Jitter.Offset.X",&jx)&&GetFloat(params_,"Jitter.Offset.Y",&jy)&&
            GetFloat(params_,"MV.Scale.X",&mx)&&GetFloat(params_,"MV.Scale.Y",&my)&&
            std::isfinite(jx)&&std::isfinite(jy)&&std::isfinite(mx)&&std::isfinite(my);
        if(!valid){ReleaseCom(device);Say(State::NativeFallback,"private-image/source/queue/guide-contract");return {};}
        recording_={device_id,input_.queue->identity,reinterpret_cast<Id>(List()),token,source_record.reset};
        nr_private_sr::Shape shape{input_.rw,input_.rh,input_.ow,input_.oh,feature_.feature_id==1?feature_.perf_quality:2,
            nr_edit_policy::CarrierFlags(feature_.create_flags,g.depth_inverted!=0),device_id,input_.queue->identity,g_config_generation.load()};
        entry_=AcquireEntry(feature_,handle_,device,input_.queue,shape);ReleaseCom(device);
        if(!entry_||!ReserveReceipt(*entry_,recording_)){Say(State::Busy,"bounded-context-or-recording-pool");return {};}
        View raw_depth{},raw_motion{},edited{},original{};
        if(!StateView(List(),img.guides.depth,g.depth_format,raw_depth)||
           !StateView(List(),img.guides.motion,static_cast<unsigned>(ID3D12Resource_GetDesc(img.guides.motion).Format),raw_motion)||
           !StateView(List(),img.edited,10,edited)||!StateView(List(),img.original,10,original))return {};
        raw_motion.format=static_cast<unsigned>(NrMotionReadFormat(ID3D12Resource_GetDesc(img.guides.motion).Format));
        if(!raw_motion.format)return {};
        GuideConstants k{};k.out[0]=input_.rw;k.out[1]=input_.rh;k.depth[0]=g.depth_width;k.depth[1]=g.depth_height;
        k.motion[0]=g.motion_source_width;k.motion[1]=g.motion_source_height;k.depth_origin[0]=g.depth_x;k.depth_origin[1]=g.depth_y;
        k.motion_origin[0]=g.motion_x;k.motion_origin[1]=g.motion_y;k.motion_scale[0]=mx;k.motion_scale[1]=my;
        k.linear_depth=g.linear_depth;k.inverted=g.depth_inverted;k.projection_a=g.depth_projection_a;k.projection_b=g.depth_projection_b;
        nr_edit_dx12::Work w{};w.kernel=nr_edit_dx12::Kernel::NormalizeGuides;w.recording=recording_;
        w.width=input_.rw;w.height=input_.rh;w.constants=&k;w.constant_bytes=sizeof(k);
        w.reads[0]=raw_depth;w.reads[1]=raw_motion;w.writes[0]=Own(entry_->depth,41);w.writes[1]=Own(entry_->motion,16);
        const auto guides=nr_edit_dx12::Record(entry_->guide_kernels,List(),w);own_recorded_|=guides.recorded;
        if(!guides.recorded){for(auto& p:entry_->receipts)if(p.recording==recording_&&!p.recorded)p={};
            Say(State::Busy,"guide-normalization-declined");return {P::Declined,0,false};}
        for(auto& p:entry_->receipts)if(p.recording==recording_)p.recorded=true;
        const Source reference{source_.stream,source_.epoch,source_.sequence>1?source_.sequence-1:0};
        const float exposure=img.color.pre_exposure;
        const bool primary_changed=!img.context||!img.context->instance_id||
            img.context->instance_id!=entry_->primary_instance||img.context->history_generation!=entry_->primary_history;
        const bool reset=primary_changed||!img.guides.capture_params.reset_known||img.guides.game_reset!=0||img.color.exposure_texture!=nullptr;
        jitter_zero_=jx==0&&jy==0;camera_=ReadCamera(params_,jitter_zero_);
        reported_r_width.store(input_.rw);reported_r_height.store(input_.rh);reported_o_width.store(input_.ow);reported_o_height.store(input_.oh);
        if(feature_.feature_id==1){
            auto& d=deferred_;d.sr.recording=recording_;d.sr.source=source_;d.sr.motion_reference=reference;
            d.sr.jitter_x=jx;d.sr.jitter_y=jy;d.sr.mv_x=d.sr.mv_y=1;d.sr.reset=reset;d.sr.resource_contract_verified=true;
            d.edited=edited;d.original=original;d.clean_output=input_.output_view;d.depth=Own(entry_->depth,41);d.motion=Own(entry_->motion,16);
            d.view=entry_->view;d.config_generation=entry_->configuration;d.color_domain=feature_.generation;
            d.scene_linear_verified=d.zero_origin_rects_verified=d.ordered_queue_verified=true;
            auto& c=d.constants;c.render_size[0]=input_.rw;c.render_size[1]=input_.rh;c.output_size[0]=input_.ow;c.output_size[1]=input_.oh;
            c.input_pre_exposure=c.output_pre_exposure=exposure;c.carrier_range=g_config.edit_experiment.carrier_range;c.frame_pair_valid=1;
            rr_history_status.store(0);
            const auto result=nr_deferred_dx12::Begin(entry_->deferred,List(),d);own_recorded_|=result.recorded;
            if(result.recorded)for(auto& receipt:entry_->receipts)if(receipt.recording==recording_)receipt.backend_recorded=true;
            return nr_edit_adapters::Step(result);
        }
        auto& r=rr_;r.recording=recording_;r.source=source_;r.motion_reference=reference;r.edited=edited;r.original=original;
        r.motion_pixels=Own(entry_->motion,16);r.hardware_depth=Own(entry_->depth,41);r.clean_output=input_.output_view;
        r.view=entry_->view;r.configuration=entry_->configuration;r.color_domain=feature_.generation;
        r.scene_linear_verified=r.normalized_guides_verified=r.ordered_queue_verified=true;r.allow_spatial_only=true;r.camera_cut=reset;
        auto& c=r.constants;c.render_size[0]=input_.rw;c.render_size[1]=input_.rh;c.output_size[0]=input_.ow;c.output_size[1]=input_.oh;
        c.input_pre_exposure=c.output_pre_exposure=exposure;c.new_sample_rate=g_config.edit_experiment.history_rate;c.frame_pair_valid=1;
        float projection[16]{};nr_rr_depth::Projection p{};
        const bool projected=ReadMatrix(params_,"ViewToClipMatrix",projection)&&nr_rr_depth::Resolve(projection,p);
        projection_known_=projected;
        if(projected){projection_[0]=c.current_projection_ab[0]=p.a;projection_[1]=c.current_projection_ab[1]=p.b;}
        if(!projected){c.padding=1u|(g.depth_inverted?2u:0u);c.depth_absolute_tolerance=.001f;c.depth_relative_tolerance=0;}
        c.previous_projection_ab[0]=entry_->previous_projection[0];c.previous_projection_ab[1]=entry_->previous_projection[1];
        const bool phase=jitter_zero_&&entry_->previous_jitter_zero&&entry_->previous_projection_known&&projected&&(feature_.create_flags&4)==0;
        c.camera_mapping_verified=g_config.edit_experiment.rr_history&&phase&&!reset&&entry_->previous_source==reference&&
            nr_edit_policy::PreviousClip(camera_,entry_->previous_camera,c.current_clip_to_previous_clip);
        rr_history_status.store(!g_config.edit_experiment.rr_history?1:!projected||!jitter_zero_||!camera_.verified?2:c.camera_mapping_verified?4:3);
        const auto result=nr_rr_dx12::Begin(entry_->rr,List(),r);own_recorded_|=result.recorded;
        if(result.recorded)for(auto& receipt:entry_->receipts)if(receipt.recording==recording_)receipt.backend_recorded=true;
        return nr_edit_adapters::Step(result);
    }
    bool RestoreBeforeState(){if(!before_||!before_->active)return false;before_->Restore();return !g_lifecycle_tracking_uncertain.load();}
    std::uint32_t CallNative(){
        auto* timed=image_.private_image.slot;
        if(timed)nr_dual_runtime::Stamp(List(),*timed,8);
        const auto value=NrCallNativeWithFormatObservation(original_,native_list_,handle_,params_,callback_,feature_.generation);
        if(timed){nr_dual_runtime::Stamp(List(),*timed,9);nr_dual_runtime::ResolveTiming(List(),*timed,value==kNgxSuccess);}
        return value;
    }
    bool PostIdentityStillMatches(){
        const auto f=FindFeature(handle_);ID3D12Resource* output=nullptr;unsigned long long token=0;
        const auto s=nr_source_runtime::CurrentSource();
        return f.handle==handle_&&f.generation==feature_.generation&&f.feature_id==feature_.feature_id&&!f.pending&&!f.retiring&&
            !OtherNrOwnerActive()&&!g_lifecycle_tracking_uncertain.load()&&!g_feature_tracking_uncertain.load()&&
            entry_&&entry_->configuration==g_config_generation.load()&&
            GetResource(params_,"Output",&output)&&output==input_.output&&
            ReadLifecycleToken(List(),token)&&token==recording_.lifetime&&Source{s.stream,s.epoch,s.sequence}==source_&&source_reset_matches();
    }
    bool source_reset_matches(){nr_source_runtime::Lock lock;
        const auto rec=nr_source_runtime::RecordingOf(List());return rec&&rec.reset==recording_.reset;}
    bool CaptureAfterState(){after_.emplace(native_list_);return after_->active&&after_->command_list==List();}
    nr_edit_flow::Step FinishEdit(std::uint32_t native){
        View output{};if(!StateView(List(),input_.output,10,output))return {};
        nr_edit_flow::Step step{};
        if(entry_->deferred){deferred_.clean_output=output;step=nr_edit_adapters::Step(nr_deferred_dx12::Finish(entry_->deferred,List(),deferred_,native));}
        else{rr_.clean_output=output;step=nr_edit_adapters::Step(nr_rr_dx12::Finish(entry_->rr,List(),rr_,native));}
        own_recorded_|=step.recorded;
        if(step.phase==nr_edit_flow::Phase::Composed){
            if(!RecordVisibleTransferLocked(List(),feature_.generation,2))return {nr_edit_flow::Phase::Fatal,0xBAD00005u,true};
            Visible(*entry_,recording_);composed.fetch_add(1);
            entry_->primary_instance=image_.private_image.context->instance_id;
            entry_->primary_history=image_.private_image.context->history_generation;
            entry_->previous_source=source_;entry_->previous_camera=camera_;entry_->previous_jitter_zero=jitter_zero_;
            entry_->previous_projection_known=projection_known_;entry_->previous_projection[0]=projection_[0];entry_->previous_projection[1]=projection_[1];
            Say(entry_->deferred?State::DeferredRecorded:rr_.constants.camera_mapping_verified?State::RrHistoryRecorded:State::RrSpatialRecorded,
                entry_->deferred?"Deferred-recorded":rr_.constants.camera_mapping_verified?"RR-history-recorded":"RR-spatial-recorded; temporal-proof-unavailable-or-cold");
        }else Say(State::PrivateSrWarmup,"private-SR-creation/warmup-or-after-decline; clean-native-output-kept");
        return step;
    }
    bool RestoreAfterState(){if(!after_||!after_->active)return false;after_->Restore();return !g_lifecycle_tracking_uncertain.load();}
    void CancelPendingEdit(){if(entry_){nr_deferred_dx12::Cancel(entry_->deferred);nr_rr_dx12::Cancel(entry_->rr);entry_->previous_camera={};}}
    void QuarantineAndAbandon(){if(entry_)Poison(*entry_);if(before_)before_->AbandonAfterOpaqueFailure();if(after_)after_->AbandonAfterOpaqueFailure();QuarantineAbortedRrTransactionLocked(List());}
};
bool TryProcess(EvaluateFeatureFn original,ID3D12GraphicsCommandList* list,const NgxHandle* handle,
                const NgxParams* params,void* callback,const FeatureRecord& feature,NgxResult& result){
    if(!g_config.enabled||!nr_edit_policy::Selected(g_config.edit_experiment,feature.feature_id))return false;
    const float active_strength=ActiveTransferStrength(g_config);
    if(!std::isfinite(active_strength)||active_strength<=0.000001f)return false;
    if(g_config.identity_use_original){Say(State::NativeFallback,"identity-mode: legacy-route-kept");return false;}
    Preflight preflight{};
    if(!PreflightInput(list,params,feature,preflight)){Say(State::NativeFallback,"needs-proven-same-queue-FP16-HDR-full-raster-state; legacy-route-kept");return false;}
    Transaction transaction(original,list,handle,params,callback,feature,preflight);
    const auto r=nr_edit_flow::Execute(transaction);result=r.result;return true;
}
}
