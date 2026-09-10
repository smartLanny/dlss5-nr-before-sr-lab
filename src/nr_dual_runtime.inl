#include "nr_dual_codec.inl"
// Included after the actual Core guide/NGX/lifetime/codec declarations.
namespace nr_dual_runtime {
enum class Status : unsigned { Off, PrimaryWarming, Preparing, CreatePending, Active,
    UnsupportedGuides, ResourceBudget, QueuePending, SlotPending, Failed };
std::atomic<Status> status{Status::Off};
std::atomic<unsigned> primary_width{0},primary_height{0},second_width{0},second_height{0};
std::atomic<unsigned long long> second_evaluations{0},second_creates{0},second_completed{0};
std::atomic<unsigned long long> last_primary_instance{0},last_second_instance{0},last_parent_evaluation{0};
std::atomic<NgxResult> fatal_result{0};
std::atomic<const NgxHandle*> fatal_carrier{nullptr};
nr_dual::dx12::Pipelines* pipelines=nullptr;
ID3D12Device* pipeline_device=nullptr;
}
#include "nr_dual_timing.inl"
namespace nr_dual_runtime {
struct Prepared {
    NrContext* context=nullptr;
    NrSlot* slot=nullptr;
    nr_dual::Plan plan{};
    NrGuideBinding guides{};
    bool create_completed=false;
    explicit operator bool() const {return context&&slot&&bool(plan);}
};
void SetStatus(Status value){status.store(value,std::memory_order_relaxed);}
void ClearSelection(NrSlot& slot){slot.dual_capture_index=nr_replay::Recorder::Invalid;slot.dual_selected=nullptr;slot.dual_reduced=false;slot.dual_low_width=slot.dual_low_height=0;}
template<class Slot> void InitializeSourceFormats(Slot& slot) {
    slot.source_format=slot.result_uav_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    // The production source adapter adds a separate typed SRV field. Both
    // source shapes use the same ordinary FP16 auxiliary allocation.
    if constexpr(requires {slot.source_srv_format;})slot.source_srv_format=slot.source_format;
}

bool NewHeap(ID3D12Device* device,ID3D12DescriptorHeap** heap) {
    if(*heap)return true;
    D3D12_DESCRIPTOR_HEAP_DESC d{};d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    d.NumDescriptors=kCodecDescriptorCount;d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    return SUCCEEDED(ID3D12Device_CreateDescriptorHeap(device,&d,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(heap)))&&*heap;
}
bool CreateCompleted(NrContext& context) {
    if(context.dual_create_completed)return true;
    // Zero fences and CPU Create success are deliberately insufficient.
    if(!context.nr_handle||context.pending_recording_token||context.awaiting_fence_signal||
       !context.completion_fence_value||!context.last_submission_queue||
       !context.last_submission_queue->fence||!IsContextFenceCompleteLocked(context))return false;
    context.dual_create_completed=true;
    Log("[dual-nr] create completed: instance=%llu queue=%llu fence=%llu; independent layer admitted.",
        context.instance_id,context.last_submission_queue->identity,context.completion_fence_value);
    return true;
}
Prepared Prepare(ID3D12Device* device,ID3D12GraphicsCommandList* list,
    NrContext& primary,NrSlot& main,const NgxParams* params,const FeatureRecord& feature,
    unsigned render_width,unsigned render_height,ID3D12Resource* original,
    unsigned x,unsigned y,CodecColorMode color_mode) {
    ClearSelection(main);
    if(CanMutateSlotLocked(main)){CollectTiming(main);main.dual_time_active=false;}
    if(g_config.nr_passes!=2){
        for(auto& child:g_contexts)if(child.layer_index==1&&child.parent_instance_id==primary.instance_id)child.retired=true;
        SetStatus(Status::Off);return {};
    }
    if(fatal_result.load()){SetStatus(Status::Failed);return {};}
    if(!primary.nr_handle||!primary.instance_id||g_config.identity_use_original||
       primary.successful_evaluations_since_reset<=g_config.warmup_after_create){SetStatus(Status::PrimaryWarming);return {};}
    const auto plan=nr_dual::MakePlan({g_config.nr_passes,{g_config.nr_second_numerator,g_config.nr_second_denominator}},
        {primary.width,primary.height},{{1,1},{16384,16384},{1,1}});
    if(!plan){SetStatus(Status::ResourceBudget);return {};}
    primary_width.store(primary.width);primary_height.store(primary.height);
    second_width.store(plan.secondary.width);second_height.store(plan.secondary.height);
    NrGuideBinding guides{};
    if(!ResolveNrGuideBinding(params,feature,primary.mode,plan.secondary.width,plan.secondary.height,
        render_width,render_height,original,x,y,color_mode,guides)||!ResolveRrLinearDepth(params,feature,guides)) {
        SetStatus(Status::UnsupportedGuides);return {};
    }
    // The first answer's phase has no verified raw-game jitter correspondence.
    // Preserve the original SR params, and never invent a known-zero phase.
    guides.jitter.known=false;
    auto* aux=FindOrCreateContext(primary.sr_handle,feature.generation,primary.mode,
        plan.secondary.width,plan.secondary.height,color_mode,guides.signature,
        primary.codec_peak_shoulder,1,primary.instance_id,primary.history_generation);
    if(!aux||aux==&primary||aux->retired||aux->failed){SetStatus(Status::SlotPending);return {};}
    if(!CanBeginContextUseLocked(list,*aux,false)){SetStatus(Status::QueuePending);return {};}
    const bool completed=CreateCompleted(*aux);
    if(aux->nr_handle&&!completed){SetStatus(Status::CreatePending);return {};}
    if(!pipelines) {
        std::string error;pipelines=nr_dual::dx12::CreatePipelines(device,error);
        if(!pipelines){Log("[dual-nr] pipeline unavailable: %s",error.c_str());SetStatus(Status::ResourceBudget);return {};}
        pipeline_device=device;
    }
    if(!IsSameRrDepthDevice(pipeline_device,device)){SetStatus(Status::ResourceBudget);return {};}
    // The same 8 primary slots map to at most 8 independent secondary slots.
    // An occupied slot is never recycled while its Feature may retain Create
    // descriptors. A primary resource replacement retires the entire child.
    const auto index=static_cast<unsigned>(&main-primary.slots);
    if(index>=8){SetStatus(Status::SlotPending);return {};}
    auto& slot=aux->slots[index];
    if(slot.key&&slot.key!=main.neural){aux->retired=true;SetStatus(Status::SlotPending);return {};}
    if(!CanMutateSlotLocked(slot)){SetStatus(Status::SlotPending);return {};}
    const auto domain=color_mode==CodecColorMode::ModelEncodedValueDomain ||
        ((feature.create_flags&1)==0 && color_mode==CodecColorMode::Automatic)
        ?nr_dual::ModelDomain::DirectValue:nr_dual::ModelDomain::SrgbProxy;
    if(plan.path==nr_dual::Path::MatchedResidual && !EnsureCodecSet(static_cast<unsigned>(domain),g_config.face_protection||g_config.face_preview)){SetStatus(Status::ResourceBudget);return {};}
    if(!slot.dual_spatial) {
        const auto available=g_dual_allocated_bytes.load()<kDualTotalBudget?
            std::min(kDualSlotBudget,kDualTotalBudget-g_dual_allocated_bytes.load()):0ull;
        std::string error;
        slot.dual_spatial=nr_dual::dx12::CreateSpatialSlot(pipelines,main.neural,plan,domain,available,error);
        if(!slot.dual_spatial){Log("[dual-nr] resources unavailable: %s",error.c_str());SetStatus(Status::ResourceBudget);return {};}
        slot.dual_owner=aux;slot.dual_bytes=nr_dual::dx12::SlotBytes(slot.dual_spatial);
        g_dual_allocated_bytes.fetch_add(slot.dual_bytes);
        const auto surfaces=nr_dual::dx12::GetSurfaces(slot.dual_spatial);
        slot.key=main.neural;slot.source=main.neural;ID3D12Resource_AddRef(slot.source);
        slot.proxy=surfaces.input;ID3D12Resource_AddRef(slot.proxy);
        slot.neural=surfaces.output;ID3D12Resource_AddRef(slot.neural);
        InitializeSourceFormats(slot);
        slot.codec_color_mode=color_mode;
    }
    if(!NewHeap(device,&slot.heap)||!NewHeap(device,&slot.dual_consumer_heap)){SetStatus(Status::ResourceBudget);return {};}
    if(main.region_ready&&!NewHeap(device,&slot.dual_region_heap)){SetStatus(Status::ResourceBudget);return {};}
    WriteCodecDescriptorSet(device,slot,slot.heap);
    if(guides.signature.resample_motion) {
        const bool allocate=!slot.motion_proxy;
        unsigned long long bytes=0;
        if(allocate) {
            auto d=ID3D12Resource_GetDesc(slot.proxy);d.Format=DXGI_FORMAT_R16G16_FLOAT;
            bytes=ID3D12Device_GetResourceAllocationInfo(device,0,1,&d).SizeInBytes;
            if(bytes>kDualSlotBudget-slot.dual_bytes || bytes>kDualTotalBudget-g_dual_allocated_bytes.load()){SetStatus(Status::ResourceBudget);return {};}
        }
        if(!EnsureMotionProxy(device,slot,guides.motion,aux->width,aux->height)){SetStatus(Status::UnsupportedGuides);return {};}
        if(allocate){slot.dual_bytes+=bytes;g_dual_allocated_bytes.fetch_add(bytes);}
    }
    if(!EnsureRrDepthSlot(device,slot,guides)){SetStatus(Status::UnsupportedGuides);return {};}
    for(unsigned i=0;i<2;++i){auto* resource=i?guides.motion:guides.depth;
        if(slot.dual_guides[i]!=resource){ID3D12Resource_AddRef(resource);ReleaseCom(slot.dual_guides[i]);slot.dual_guides[i]=resource;}}
    if(!aux->params) {
        NgxResult result=0;
        {ScopedSrwExclusive lock(&g_nr_api_lock);result=g_nr_api.allocate(&aux->params);}
        if(result!=kNgxSuccess||!aux->params){aux->failed=true;SetStatus(Status::ResourceBudget);return {};}
    }
    if(aux->params==primary.params||aux->nr_handle&&aux->nr_handle==primary.nr_handle){
        aux->lifecycle_dependencies_quarantined=true;primary.lifecycle_dependencies_quarantined=true;
        MarkLifecycleUncertain("Independent NR layer identity aliased the primary");SetStatus(Status::Failed);return {};
    }
    PrepareTiming(device,main);
    SetStatus(completed?Status::Preparing:Status::CreatePending);
    return {aux,&slot,plan,guides,completed};
}

NgxResult Fail(ID3D12GraphicsCommandList* list,NrContext& primary,NrContext& aux,NgxResult result,const char* stage) {
    if(result==0||result==kNgxSuccess)result=0xBAD00005u;
    primary.lifecycle_dependencies_quarantined=aux.lifecycle_dependencies_quarantined=true;
    aux.failed=true;aux.dual_history.Invalidate();
    fatal_carrier.store(primary.sr_handle,std::memory_order_release);fatal_result.store(result,std::memory_order_release);
    unsigned long long token=0;
    if(ReadLifecycleToken(list,token)&&token)QuarantineUnsubmittedLifecycleRecordLocked(token,"after opaque secondary NR failure");
    MarkLifecycleUncertain("Opaque second NR call failed; no further commands or retry on the affected chain");
    SetStatus(Status::Failed);
    Log("[dual-nr] FATAL stage=%s result=0x%08X parent=%llu auxiliary=%llu; complete command-list dependencies retained; no SR/combine/state restore; restart required.",stage,result,primary.instance_id,aux.instance_id);
    return result;
}
std::uint32_t Evaluate(void* user,ID3D12GraphicsCommandList* list,const void* feature,const void* params) {
    auto& aux=*static_cast<NrContext*>(user);
    if(aux.nr_handle!=feature||aux.params!=params||aux.failed)return 0xBAD00005u;
    ScopedSrwExclusive lock(&g_nr_api_lock);
    return g_nr_api.bridge_evaluate(g_nr_api.signed_evaluate,list,aux.nr_handle,aux.params,nullptr);
}
NgxResult Run(ID3D12GraphicsCommandList* list,NrContext& primary,NrSlot& main,
    Prepared& prepared,const FeatureRecord& feature,const ColorContract& color) {
    if(!prepared)return 0;
    auto& aux=*prepared.context;auto& slot=*prepared.slot;
    if(primary.diagnostic_reset_requested||primary.successful_evaluations_since_reset<=g_config.warmup_after_create){
        aux.dual_history.Invalidate();SetStatus(Status::PrimaryWarming);return 0;
    }
    if(!RecordNrUseLocked(list,aux,slot)){SetStatus(Status::QueuePending);return 0;}
    const auto& g=prepared.guides.signature;
    if(g.resample_motion) {
        Transition(list,slot.motion_proxy,slot.motion_proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        DispatchMotionResample(list,slot,aux.width,aux.height,g.motion_source_width,g.motion_source_height,g.motion_x,g.motion_y);
        UavBarrier(list,slot.motion_proxy);
        Transition(list,slot.motion_proxy,slot.motion_proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if(!DispatchRrDepth(list,slot,prepared.guides)){SetStatus(Status::UnsupportedGuides);return 0;}
    const nr_dual::LayerKey key{1,primary.instance_id,primary.history_generation,aux.instance_id?aux.instance_id:1,prepared.plan.secondary};
    const bool reset=aux.dual_history.NeedsReset(key,primary.evaluations,primary.diagnostic_reset_requested);
    if(!BindNrParameters(aux,slot,prepared.guides,feature,aux.width,aux.height,color,reset,slot.motion_proxy)) {
        aux.dual_history.Invalidate();SetStatus(Status::UnsupportedGuides);return 0;
    }
    const auto surfaces=nr_dual::dx12::GetSurfaces(slot.dual_spatial);
    auto state=static_cast<std::uint32_t>(main.neural_state);
    if(!aux.nr_handle) {
        if(!nr_dual::dx12::RecordCreationInputs(pipelines,slot.dual_spatial,list,state)){SetStatus(Status::ResourceBudget);return 0;}
        main.neural_state=static_cast<D3D12_RESOURCE_STATES>(state);
        for(unsigned i=0;i<2;++i){auto* resource=i?(g.resample_motion?slot.motion_proxy:prepared.guides.motion):(g.linear_depth?slot.rr_depth:prepared.guides.depth);
            ID3D12Resource_AddRef(resource);aux.dual_create_guides[i]=resource;}
        void* handle=nullptr;const auto created=CreateSignedNrFeature(list,aux,feature,&handle);
        aux.nr_handle=static_cast<NgxHandle*>(handle);second_creates.fetch_add(1);
        if(created!=kNgxSuccess||!handle)return Fail(list,primary,aux,created,"Create");
        Log("[dual-nr] independent create parent=%llu auxiliary=%llu N1=%ux%u N2=%ux%u; waiting exact completion, no Evaluate in creation recording.",primary.instance_id,aux.instance_id,primary.width,primary.height,aux.width,aux.height);
        SetStatus(Status::CreatePending);return 0;
    }
    nr_dual::Admission admission{};
    admission.primary_feature=primary.nr_handle;admission.secondary_feature=aux.nr_handle;
    admission.primary_params=primary.params;admission.secondary_params=aux.params;
    admission.secondary_create_extent={aux.width,aux.height};
    admission.creation_completion_proven=prepared.create_completed&&aux.dual_create_completed;
    admission.recording_and_queue_admitted=CanBeginContextUseLocked(list,primary,false)&&CanBeginContextUseLocked(list,aux,false);
    admission.dependencies_pinned=primary.pending_recording_token!=0&&primary.pending_recording_token==aux.pending_recording_token&&main.recording_command_lists!=0&&slot.recording_command_lists!=0;
    admission.current_inputs_and_guides_bound=slot.proxy==surfaces.input&&slot.neural==surfaces.output&&
        slot.capture_params.depth_width==g.depth_width&&slot.capture_params.mv_width==g.motion_bound_width;
    const auto attempt=nr_dual::dx12::RecordSecond(pipelines,slot.dual_spatial,list,state,prepared.plan,admission,Evaluate,&aux,Clock,&main);
    main.neural_state=static_cast<D3D12_RESOURCE_STATES>(state);
    if(attempt.selection==nr_dual::Selection::NoneAfterOpaqueFailure)return Fail(list,primary,aux,attempt.ngx_result,"Evaluate");
    if(!attempt.HasSecond()){aux.dual_history.Invalidate();SetStatus(Status::QueuePending);return 0;}
    aux.guide_history.CommitOnEvaluate(prepared.guides.signature,true);
    ++aux.evaluations;
    aux.successful_evaluations_since_reset=aux.diagnostic_reset_requested?1:aux.successful_evaluations_since_reset+1;
    aux.reset_retry_pending=false;
    aux.dual_history.CommitSuccess(key,primary.evaluations);
    second_evaluations.fetch_add(1);last_primary_instance.store(primary.instance_id);last_second_instance.store(aux.instance_id);last_parent_evaluation.store(primary.evaluations);
    if(aux.successful_evaluations_since_reset<=g_config.warmup_after_create){SetStatus(Status::Preparing);return 0;}
    main.dual_selected=&slot;main.dual_reduced=attempt.selection==nr_dual::Selection::FusedResidual;
    main.dual_low_width=aux.width;main.dual_low_height=aux.height;main.dual_model_domain=static_cast<unsigned>(surfaces.domain);
    // This distinct heap was allocated and proved free in Prepare. It has not
    // been used by Encode/NR/downsample or any earlier dispatch of this recording.
    WriteCodecDescriptorSet(g_codec.device,main,slot.dual_consumer_heap);
    if(main.region_ready&&slot.dual_region_heap) {
        nr_regions::WriteDescriptors(g_codec.device,main,slot.dual_region_heap,main.dual_reduced?main.neural:surfaces.output,main.dual_reduced?surfaces.low_delta:nullptr);
        main.region_constants[16]=aux.width;main.region_constants[17]=aux.height;
        main.region_constants[18]=main.dual_reduced?1:0;main.region_constants[19]=main.dual_model_domain;
    }
    nr_dual_capture::Begin(list,primary,main,aux,slot,prepared.guides);
    SetStatus(Status::Active);
    if(aux.evaluations<=3||aux.evaluations%600==0)
        Log("[dual-nr] recorded parent=%llu:%u auxiliary=%llu:%u N1=%ux%u N2=%ux%u scale=%u/%u domain=%u phase=unknown final=%s; CPU Evaluate success, GPU completion pending.",primary.instance_id,primary.evaluations,aux.instance_id,aux.evaluations,primary.width,primary.height,aux.width,aux.height,prepared.plan.requested.numerator,prepared.plan.requested.denominator,main.dual_model_domain,main.dual_reduced?"fused-matched-residual":"direct-P2");
    return 0;
}
}
