// Include AFTER nr_compat_present_runtime.inl. This records on the actual FG
// input command list and restores its native state. It never closes, executes,
// flushes, or signals the game's list, and never calls native SR/FG itself.
namespace nr_fg_runtime {
using nr_source_runtime::Id;
using nr_source_runtime::NativeInputs;
namespace io=nr_compat_present_runtime;
struct Session {
    Id stream=0,epoch=0,generation=0;
    alignas(void*) unsigned char opaque[sizeof(void*)]{};
    io::Context images{};
    NgxParams* metadata=nullptr;
    NrContext* primary=nullptr;
    ID3D12Resource *full_raw=nullptr,*alpha_raw=nullptr,*scene_encoded=nullptr;
    ID3D12RootSignature* hud_root=nullptr;
    ID3D12PipelineState* hud_pso=nullptr;
    ID3D12DescriptorHeap* hud_heap=nullptr;
    DXGI_FORMAT alpha_format=DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES full_state=D3D12_RESOURCE_STATE_COMMON,alpha_state=D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES scene_state=D3D12_RESOURCE_STATE_COMMON;
    struct Rtv {nr_source_frame::Resource resource{};ID3D12DescriptorHeap* heap=nullptr;} rtvs[8]{};
    bool failed=false;
};
Session sessions[8]{};
std::atomic<unsigned long long> attempts{0},recorded{0},adopted{0},unknown_inputs{0},hudless_waiting{0};
std::atomic<unsigned long long> last_adopted_tick{0};
std::atomic<unsigned long long> last_attempt_tick{0};
std::atomic<unsigned> last_adopted_generation{0};
bool RecentlyRecorded(unsigned generation){
    const auto tick=last_adopted_tick.load(std::memory_order_acquire);
    return tick&&last_adopted_generation.load(std::memory_order_relaxed)==generation&&GetTickCount64()-tick<1500;
}
bool CapturePreferred(){return last_attempt_tick.load(std::memory_order_acquire)>io::last_recorded_tick.load(std::memory_order_acquire);}
void AppendFeedback(FeedbackSession& text,const char* prefix){
    FeedbackAppend(text,"%s.fgConsumer.attempted=%llu\n%s.fgConsumer.recorded=%llu\n%s.fgConsumer.composed=%llu\n"
        "%s.fgConsumer.unknownInputs=%llu\n%s.fgConsumer.hudlessUnavailable=%llu\n"
        "%s.fgConsumer.proof=commands-recorded-before-native-FG; not physical presentation proof\n",
        prefix,attempts.load(),prefix,recorded.load(),prefix,adopted.load(),prefix,unknown_inputs.load(),prefix,hudless_waiting.load(),prefix);
}
Id next_generation=1;
const NgxHandle* Handle(Session& s){return reinterpret_cast<const NgxHandle*>(s.opaque);}
bool Idle(Session& s){
    // Core slots can retire/reuse an address after a tuning change. Resolve by
    // the private owner, including every layer, never by a retained slot pointer.
    for(const auto& context:g_contexts)
        if(context.sr_handle==Handle(s)&&!CanDestroyContextLocked(context))return false;
    for(const auto& context:g_pending_destroy_contexts)
        if(context.sr_handle==Handle(s)&&!CanDestroyContextLocked(context))return false;
    return true;
}
void ReleaseImages(Session& s){
    // Caller proved no GPU use. Core slots still retain any resources referenced
    // by deferred NGX teardown, independently of these transport references.
    auto& c=s.images;ReleaseCom(c.raw);ReleaseCom(c.image.source);ReleaseCom(c.image.result);
    ReleaseCom(c.io);ReleaseCom(c.blit);ReleaseCom(c.convert);ReleaseCom(c.init);ReleaseCom(c.init_color);ReleaseCom(c.util_root);
    for(auto& r:s.rtvs){ReleaseCom(r.heap);r={};}
    ReleaseCom(s.full_raw);ReleaseCom(s.alpha_raw);ReleaseCom(s.scene_encoded);
    ReleaseCom(s.hud_root);ReleaseCom(s.hud_pso);ReleaseCom(s.hud_heap);
    s.alpha_format=DXGI_FORMAT_UNKNOWN;s.full_state=s.alpha_state=s.scene_state=D3D12_RESOURCE_STATE_COMMON;
    ReleaseCom(c.device);c={};s.primary=nullptr;
}
void CollectRetired(){
    if(!TryEnterCriticalSection(&g_chain_cs))return;
    struct Unlock{~Unlock(){LeaveCriticalSection(&g_chain_cs);}} unlock;
    for(auto& s:sessions){if(!s.stream)continue;
        bool active=false;{nr_source_runtime::Lock lock;active=nr_source_runtime::ProducerActive({s.stream,s.epoch,1});}
        if(active&&nr_processing_runtime::work_enabled.load()&&nr_processing_runtime::PresentSelected())continue;
        RetireContextForHandle(Handle(s));if(!Idle(s))continue;
        ReleaseImages(s);
        if(s.metadata){ScopedSrwExclusive api(&g_nr_api_lock);
            if(!g_nr_api.destroy||g_nr_api.destroy(s.metadata)!=kNgxSuccess)continue;
            s.metadata=nullptr;}
        s={};
    }
}
Session* Find(const NativeInputs& input,ID3D12Device* device,unsigned w,unsigned h){
    const auto format=ID3D12Resource_GetDesc(input.color).Format;
    Session* free=nullptr;
    for(auto& s:sessions){
        if(s.stream==input.source.stream&&s.epoch==input.source.epoch){
            if(s.failed)return nullptr;
            if(s.images.device&&(!IsSameRrDepthDevice(device,s.images.device)||s.images.source.width!=w||s.images.source.height!=h||
               s.images.format!=format||static_cast<unsigned>(s.images.encoding)!=input.encoding)){
                if(!Idle(s))return nullptr;RetireContextForHandle(Handle(s));ReleaseImages(s);s.generation=next_generation++;
            }
            return &s;
        }
        if(!s.stream&&!free)free=&s;
    }
    if(!free||next_generation==UINT64_MAX)return nullptr;
    free->stream=input.source.stream;free->epoch=input.source.epoch;free->generation=next_generation++;return free;
}
bool BuildImages(Session& s,ID3D12Device* device,const NativeInputs& input,unsigned w,unsigned h){
    auto& c=s.images;if(c.device)return c.raw&&c.image.source&&c.image.result&&c.util_root&&c.convert&&c.blit&&c.io&&s.metadata;
    c.device=device;ID3D12Device_AddRef(device);c.source={w,h};c.work=c.source;
    c.format=ID3D12Resource_GetDesc(input.color).Format;c.encoding=static_cast<nr_compat_present::Encoding>(input.encoding);
    if(!io::Texture(c,c.source,c.format,D3D12_RESOURCE_FLAG_NONE,&c.raw)||
       !io::Texture(c,c.source,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,&c.image.source)||
       !io::Texture(c,c.source,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,&c.image.result)||
       !io::MakePipelines(c)||!io::Table(c,&c.io,c.image.source))return false;
    if(!s.metadata){ScopedSrwExclusive api(&g_nr_api_lock);if(g_nr_api.allocate(&s.metadata)!=kNgxSuccess||!s.metadata)return false;}
    return true;
}
ID3D12DescriptorHeap* Rtv(Session& s,ID3D12Resource* resource){
    nr_source_frame::Resource key{};{nr_source_runtime::Lock lock;key=nr_source_runtime::ResourceOf(resource);}
    for(auto& r:s.rtvs)if(r.resource==key&&r.heap)return r.heap;
    bool free=false;for(const auto& r:s.rtvs)free|=r.heap==nullptr;
    if(!free&&Idle(s)){ReleaseCom(s.rtvs[0].heap);s.rtvs[0]={};}
    for(auto& r:s.rtvs)if(!r.heap){D3D12_DESCRIPTOR_HEAP_DESC h{};h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;h.NumDescriptors=1;
        if(FAILED(ID3D12Device_CreateDescriptorHeap(s.images.device,&h,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(&r.heap))))return nullptr;
        D3D12_RENDER_TARGET_VIEW_DESC view{};view.Format=s.images.format;view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateRenderTargetView(s.images.device,resource,&view,ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(r.heap));
        r.resource=key;return r.heap;}
    return nullptr;
}
void Metadata(Session& s,const NativeInputs& input,const NrGuideBinding& guides){
    auto* p=s.metadata;SetResource(p,"Color",s.images.image.result);SetResource(p,"Output",s.images.image.result);
    SetResource(p,"Depth",guides.depth);SetResource(p,"MotionVectors",guides.motion);
    SetUInt(p,"Width",guides.signature.depth_width);SetUInt(p,"Height",guides.signature.depth_height);
    SetUInt(p,"OutWidth",s.images.source.width);SetUInt(p,"OutHeight",s.images.source.height);
    SetUInt(p,"DLSS.Render.Subrect.Dimensions.Width",guides.signature.depth_width);
    SetUInt(p,"DLSS.Render.Subrect.Dimensions.Height",guides.signature.depth_height);
    SetUInt(p,"DLSS.Input.Depth.Subrect.Base.X",guides.signature.depth_x);SetUInt(p,"DLSS.Input.Depth.Subrect.Base.Y",guides.signature.depth_y);
    SetUInt(p,"DLSS.Input.MV.Subrect.Base.X",guides.signature.motion_x);SetUInt(p,"DLSS.Input.MV.Subrect.Base.Y",guides.signature.motion_y);
    SetFloat(p,"MV.Scale.X",guides.signature.motion_scale_x);SetFloat(p,"MV.Scale.Y",guides.signature.motion_scale_y);
    SetInt(p,"Reset",input.reset?1:0);
}
void Draw(Session& s,ID3D12GraphicsCommandList* list,ID3D12Resource* buffer,ID3D12DescriptorHeap* rtv,unsigned original_state){
    auto& c=s.images;auto state=static_cast<D3D12_RESOURCE_STATES>(original_state);
    Transition(list,buffer,state,D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12DescriptorHeap* heaps[]={c.io};ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,heaps);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,c.util_root);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,0,ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(c.io));
    const unsigned constants[]={c.source.width,c.source.height,static_cast<unsigned>(c.encoding),0};
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,1,4,constants,0);
    ID3D12GraphicsCommandList_SetPipelineState(list,c.blit);
    const auto target=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv);
    ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&target,FALSE,nullptr);
    const D3D12_VIEWPORT viewport{0,0,float(c.source.width),float(c.source.height),0,1};
    const D3D12_RECT rect{0,0,LONG(c.source.width),LONG(c.source.height)};
    ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&rect);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(list,3,1,0,0);
    Transition(list,buffer,state,static_cast<D3D12_RESOURCE_STATES>(original_state));
}
#include "nr_fg_hud_runtime.inl"
bool ConsumeDetailed(ID3D12GraphicsCommandList* commands,const NativeInputs& original,bool* writeback){
    if(writeback)*writeback=false;
    NativeInputs input=original;
    auto* destination=original.output?original.output:original.color;
    const auto destination_rect=original.output?original.output_rect:original.color_rect;
    const unsigned destination_state=original.output?original.output_state:original.color_state;
    const bool destination_state_known=original.output?original.output_state_known:original.states_known;
    const bool hudless=original.hudless!=nullptr;
    if(hudless){
        if(!original.hudless_state_known||!original.color||original.color==original.hudless){++unknown_inputs;return false;}
        const auto full=ID3D12Resource_GetDesc(original.color),scene=ID3D12Resource_GetDesc(original.hudless);
        if(full.Format!=scene.Format||full.Width!=scene.Width||full.Height!=scene.Height||
           original.hudless_rect.x||original.hudless_rect.y||original.hudless_rect.width!=full.Width||original.hudless_rect.height!=full.Height){++hudless_waiting;return false;}
        if(original.ui_alpha){
            const auto alpha=ID3D12Resource_GetDesc(original.ui_alpha);
            if(!original.ui_alpha_state_known||alpha.Width!=full.Width||alpha.Height!=full.Height||alpha.MipLevels!=1||alpha.DepthOrArraySize!=1||alpha.SampleDesc.Count!=1||
               (alpha.Format!=DXGI_FORMAT_R8_UNORM&&alpha.Format!=DXGI_FORMAT_R16_FLOAT&&alpha.Format!=DXGI_FORMAT_R32_FLOAT)){++hudless_waiting;return false;}
        }
        input.color=original.hudless;input.color_rect=original.hudless_rect;input.color_state=original.hudless_state;
    }
    if(!nr_processing_runtime::PresentSelected()||!input.source||!commands)return false;
    ++attempts;
    // The output adapter records an actual graphics RT pass. A native FG
    // compute/bundle list must continue unchanged until a compute adapter exists.
    if(ID3D12GraphicsCommandList_GetType(commands)!=D3D12_COMMAND_LIST_TYPE_DIRECT){++unknown_inputs;return false;}
    if(input.distortion){++unknown_inputs;return false;}
    if(!input.color||!input.depth||!input.motion||!destination||!input.states_known||!destination_state_known||!input.encoding||input.paused){++unknown_inputs;return false;}
    const auto desc=ID3D12Resource_GetDesc(input.color),target_desc=ID3D12Resource_GetDesc(destination);
    const unsigned w=input.color_rect.width,h=input.color_rect.height;
    if(!w||!h||input.color_rect.x||input.color_rect.y||desc.Width!=w||desc.Height!=h||
       desc.DepthOrArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||
       destination_rect.x||destination_rect.y||destination_rect.width!=w||destination_rect.height!=h||
       target_desc.Width!=w||target_desc.Height!=h||target_desc.Format!=desc.Format||
       target_desc.DepthOrArraySize!=1||target_desc.MipLevels!=1||target_desc.SampleDesc.Count!=1||
       (target_desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)==0)return false;
    auto guides=nr_source_runtime::FgGuides(input);if(!guides.depth||guides.signature.linear_depth)return false;
    ScopedRrOwnershipGate ownership(true,true,true);if(!ownership.acquired()||ownership.is_nested())return false;
    EnterCriticalSection(&g_chain_cs);
    struct ChainUnlock {~ChainUnlock(){LeaveCriticalSection(&g_chain_cs);}} unlock;
    if(!g_config.enabled||g_config.identity_use_original||!std::isfinite(g_config.transfer_strength)||g_config.transfer_strength<=0.000001f||
       OtherNrOwnerActive()||g_lifecycle_tracking_uncertain.load()||g_nr_failed_create_process_latched.load())return false;
    const auto retry_token=nr_source_runtime::CurrentRetryToken();
    if(!nr_processing_runtime::session.Ready(nr_processing::Start::Present,retry_token))return false;
    const auto reject_build=[retry_token](const char* reason){nr_processing_runtime::RejectBeforePresentWork(retry_token,reason);};
    last_attempt_tick.store(GetTickCount64(),std::memory_order_release);
    NativeCommandStateScope state(commands);if(!state.active)return false;auto* list=state.command_list;
    ID3D12Device* device=nullptr;if(FAILED(ID3D12GraphicsCommandList_GetDevice(list,IID_ID3D12Device,reinterpret_cast<void**>(&device)))||!device)return false;
    struct DeviceRelease {ID3D12Device*& device;~DeviceRelease(){ReleaseCom(device);}} device_release{device};
    for(auto* resource:{input.color,input.depth,input.motion,destination,original.ui_alpha}){if(!resource)continue;ID3D12Device* owner=nullptr;
        const bool same=SUCCEEDED(ID3D12Resource_GetDevice(resource,IID_ID3D12Device,reinterpret_cast<void**>(&owner)))&&IsSameRrDepthDevice(device,owner);
        ReleaseCom(owner);if(!same)return false;}
    if(!EnsureNrApi(device)||!EnsureCodec(device)){reject_build("fg-consumer-runtime-or-codec-not-ready");return false;}
    auto* session=Find(input,device,w,h);if(!session)return false;
    if(!BuildImages(*session,device,input,w,h)){ReleaseImages(*session);reject_build("fg-consumer-resource-build-failed");return false;}
    auto& s=*session;auto& c=s.images;
    if(hudless&&!BuildHud(s,original)){if(Idle(s)){RetireContextForHandle(Handle(s));ReleaseImages(s);}return false;}
    auto* rtv=Rtv(s,destination);if(!rtv)return false;
    auto* scene_rtv=hudless?Rtv(s,s.scene_encoded):nullptr;if(hudless&&!scene_rtv)return false;
    // Private producer sentinel, never registered as a Feature1 or used for a
    // native call. The existing Core still owns all Feature18 instances.
    FeatureRecord feature{};feature.feature_id=0;feature.handle=Handle(s);feature.generation=s.generation;
    feature.width=guides.signature.depth_width;feature.height=guides.signature.depth_height;feature.out_width=w;feature.out_height=h;
    feature.create_flags=(input.encoding==1?0:1)|(input.depth_inverted?8:0);
    if(guides.signature.motion_source_width!=w||guides.signature.motion_source_height!=h)feature.create_flags|=2;
    feature.create_flags_known=true;
    guides.signature.normalized_create_flags=feature.create_flags;
    const auto mode=input.encoding==1?CodecColorMode::LinearSdr:CodecColorMode::Automatic;
    const auto work=nr_compat_present::WorkExtent({w,h},g_config.compat_percent);
    auto* context=FindOrCreateContext(Handle(s),s.generation,1,work.width,work.height,mode,guides.signature,false);
    if(!context||context->failed||context->retired||!CanBeginContextUseLocked(list,*context))return false;
    if(context->nr_handle&&!nr_dual_runtime::CreateCompleted(*context))return false;
    s.primary=context;
    ColorContract color{};color.pre_exposure=color.exposure_scale=color.source_to_canonical=1;
    color.has_pre_exposure=color.has_exposure_scale=true;NrColorSemanticContract semantic{};semantic.exposure=color;semantic.flags_known=true;
    auto* slot=FindOrCreateSlot(*context,device,c.image.result,true,nullptr,mode,feature.create_flags,&semantic,guides.motion);
    if(!slot||context->failed||context->retired||!nr_source_runtime::InlineAllowed(3))return false;
    slot->codec_color_mode=mode;
    if(!RecordNrUseLocked(list,*context,*slot))return false;
    nr_source_runtime::MarkPrivateRecorded();++recorded;
    ++g_carrier_evaluations; // Include real private FG-consumer contexts in existing retirement/queue observation.
    CaptureHud(s,list,original);
    auto source_state=static_cast<D3D12_RESOURCE_STATES>(input.color_state);
    Transition(list,input.color,source_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list,c.raw,c.raw_state,D3D12_RESOURCE_STATE_COPY_DEST);ID3D12GraphicsCommandList_CopyResource(list,c.raw,input.color);
    Transition(list,input.color,source_state,static_cast<D3D12_RESOURCE_STATES>(input.color_state));
    Transition(list,c.raw,c.raw_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(list,c.image.source,c.image.source_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    io::Utility(c,list,c.io,c.convert,c.source);UavBarrier(list,c.image.source);
    Transition(list,c.image.source,c.image.source_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list,c.image.result,c.image.result_state,D3D12_RESOURCE_STATE_COPY_DEST);ID3D12GraphicsCommandList_CopyResource(list,c.image.result,c.image.source);
    Transition(list,c.image.source,c.image.source_state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,c.image.result,c.image.result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);slot->result_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    unsigned prior[2]={input.depth_state,input.motion_state};ID3D12Resource* original_guides[]={input.depth,input.motion};
    for(unsigned i=0;i<2;++i){auto value=static_cast<D3D12_RESOURCE_STATES>(prior[i]);Transition(list,original_guides[i],value,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
    struct RestoreGuides {ID3D12GraphicsCommandList* list;ID3D12Resource** resources;unsigned* states;bool enabled=true;
        ~RestoreGuides(){if(enabled)for(unsigned i=0;i<2;++i){auto state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            Transition(list,resources[i],state,static_cast<D3D12_RESOURCE_STATES>(states[i]));}}} restore_guides{list,original_guides,prior};
    PostSrSubstitution substitution{};substitution.active=true;substitution.opaque_failure_is_fatal=true;substitution.original_output=c.image.result;
    substitution.context=context;substitution.slot=slot;substitution.width=w;substitution.height=h;
    substitution.work_width=work.width;substitution.work_height=work.height;
    substitution.guide_width=guides.signature.depth_width;substitution.guide_height=guides.signature.depth_height;
    substitution.color_contract=color;substitution.guides=guides;substitution.native_output_snapshot=slot->snapshots_native_output;
    ++context->frames_seen;Metadata(s,input,guides);
    bool changed=false;
    if(!context->nr_handle){
        // Warm Create separately and let actual Core Execute/fence retirement
        // prove completion before the next source can Evaluate this instance.
        if(!DispatchPostSrNativeSnapshot(list,substitution))return true;
        Transition(list,slot->proxy,slot->proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        DispatchCodec(list,*slot,g_codec.encode,work.width,work.height,color,(feature.create_flags&1)!=0,0,0,0,0,mode);
        UavBarrier(list,slot->proxy);Transition(list,slot->proxy,slot->proxy_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(list,slot->neural,slot->neural_state,D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyResource(list,slot->neural,slot->proxy);
        Transition(list,slot->proxy,slot->proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list,slot->neural,slot->neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if(!context->params){ScopedSrwExclusive api(&g_nr_api_lock);if(g_nr_api.allocate(&context->params)!=kNgxSuccess)context->params=nullptr;}
        if(!context->params||!BindNrParameters(*context,*slot,guides,feature,work.width,work.height,color,true))return true;
        void* handle=nullptr;const auto result=CreateSignedNrFeature(list,*context,feature,&handle);context->nr_handle=static_cast<NgxHandle*>(handle);
        if(result!=kNgxSuccess||!handle){s.failed=true;context->create_dependencies_quarantined=true;
            nr_source_runtime::ReportFgFailure(result?result:0xBAD00005u);restore_guides.enabled=false;state.AbandonAfterOpaqueFailure();return true;}
    }else{
        substitution.dual_prepared=nr_dual_runtime::Prepare(device,list,*context,*slot,s.metadata,feature,
            guides.signature.depth_width,guides.signature.depth_height,c.image.result,0,0,mode);
        nr_fg_capture::Start(list,input.source,*context);
        changed=FinishAfterSr(list,s.metadata,feature,substitution);
        nr_fg_capture::Finish(list,input,c,*context,*slot,guides,substitution.dual_prepared,changed);
        if(substitution.fatal_recording){s.failed=true;context->lifecycle_dependencies_quarantined=true;
            nr_source_runtime::ReportFgFailure(substitution.fatal_recording);restore_guides.enabled=false;state.AbandonAfterOpaqueFailure();return true;}
    }
    c.image.result_state=slot->result_state;
    if(changed){Transition(list,c.image.result,c.image.result_state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        slot->result_state=c.image.result_state;
        if(hudless){
            Draw(s,list,s.scene_encoded,scene_rtv,static_cast<unsigned>(s.scene_state));
            Transition(list,s.scene_encoded,s.scene_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
            auto state=static_cast<D3D12_RESOURCE_STATES>(original.hudless_state);
            Transition(list,original.hudless,state,D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12GraphicsCommandList_CopyResource(list,original.hudless,s.scene_encoded);
            Transition(list,original.hudless,state,static_cast<D3D12_RESOURCE_STATES>(original.hudless_state));
            DrawHud(s,list,original,rtv);
        }else Draw(s,list,destination,rtv,destination_state);
        ++adopted;nr_processing_runtime::Adopted(nr_processing::Start::Present);
        last_adopted_generation.store(g_config_generation.load(),std::memory_order_relaxed);
        last_adopted_tick.store(GetTickCount64(),std::memory_order_release);if(writeback)*writeback=true;}
    return true;
}
bool Consume(ID3D12GraphicsCommandList* commands,const NativeInputs& input){return ConsumeDetailed(commands,input,nullptr);}
void Register(){nr_source_runtime::SetFgConsumer(Consume);}
} // namespace nr_fg_runtime
