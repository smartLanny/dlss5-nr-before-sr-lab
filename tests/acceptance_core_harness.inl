// Complete production controller and configuration functions. No ReShade/game
// is launched: the opaque runtime identity is never dereferenced by this test.
#include <fstream>
#include <map>
namespace acceptance_core_test {
using queue_test::Check;
void Stage(const char* name){std::fprintf(stderr,"[acceptance-stage] %llu %s\n",
    static_cast<unsigned long long>(GetTickCount64()),name);std::fflush(stderr);}
void DrainExport(){
    const auto end=GetTickCount64()+5000;
    while(nr_acceptance::packaging.load()&&GetTickCount64()<end)Sleep(10);
    Check(!nr_acceptance::packaging.load(),"bounded export finishes");
    Check(nr_acceptance::last_restored,"actual configuration restored");
}
void WarpTiming(){
    Stage("WarpTiming begin");
    // Actual generated Core, WARP Direct list, production lifecycle and exact
    // submission fence. Frame identity is a fixture; Sample is never injected.
    queue_test::Fixture f;
    auto owner=std::make_unique<NrContext>();auto& context=*owner;auto& slot=context.slots[0];
    context.width=context.height=128;context.instance_id=501;context.feature_generation=551;
    context.mode=1;context.sr_handle=reinterpret_cast<const NgxHandle*>(501);
    g_config=ChainConfig{};g_config.enabled=true;g_config.mode=1;
    g_config.processing_start=nr_processing::Start::After;g_config.processing_selection=1;
    nr_processing_runtime::Reconcile();
    Check(nr_processing_runtime::Effective()==nr_processing::Start::After,"WARP timing uses native After route");
    queue_test::List list(f);
    list.Close();list.Prime(f.a);list.Prime(f.a);f.Drain(f.a);list.ResetWithNewAllocator(f);
    Check(FindOrCreateLifecycleQueueLocked(f.a)!=nullptr,"real timing queue tracked");
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width=4u*1024u*1024u;desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *from=nullptr,*to=nullptr;
    queue_test::Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_COPY_SOURCE,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&from)),"GPU source buffer");
    queue_test::Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&to)),"GPU target buffer");
    nr_acceptance::performance_active.store(true);
    {
        nr_source_runtime::StreamlineScope source(nr_source_frame::Source{41,51,1});
        NgxParams params{};
        nr_source_runtime::EvaluateScope evaluate(list.native,reinterpret_cast<const NgxHandle*>(501),
            &params,1,551);
        Check(nr_source_runtime::CurrentSource().sequence==1,"native source observed by timing");
        Check(nr_acceptance_gpu::Prepare(f.device,slot,list.native,128,128,128,128),
            "mutable slot prepares real timestamp resources before token exists");
        Check(slot.acceptance_time_prepared&&!slot.acceptance_time_token,"prepare does not guess recording token");
        Check(RecordNrUseLocked(list.native,context,slot),"actual NR lifecycle use records");
        Check(nr_acceptance_gpu::Bind(list.native,slot),"timestamp binds successful exact recording token");
        Check(slot.acceptance_time_token&&slot.acceptance_time_active,"bound slot owns exact token");
        slot.acceptance_time_identity.instance_id=501;slot.acceptance_time_identity.frame_id=1;
        slot.acceptance_time_identity.config_generation=g_config_generation.load();
        slot.acceptance_time_identity.evaluation=1;slot.acceptance_time_identity.since_reset=64;
        slot.acceptance_time_identity.warmed_up=true;
        nr_acceptance_gpu::Stamp(list.native,slot,0);
        ID3D12GraphicsCommandList_CopyBufferRegion(list.native,to,0,from,0,desc.Width);
        nr_acceptance_gpu::Stamp(list.native,slot,1);
        nr_acceptance_gpu::Stamp(list.native,slot,2);
        ID3D12GraphicsCommandList_CopyBufferRegion(list.native,to,0,from,0,desc.Width);
        nr_acceptance_gpu::Finish(list.native,slot,true);
        Check(slot.acceptance_time_pending&&slot.acceptance_time_mask==15,"four real GPU queries resolved");
        Check(RecordVisibleTransferLocked(list.native,1,1),"visible NR source bound to submission");
    }
    list.Close();list.Submit(f.a);
    Check(slot.acceptance_time_queue&&slot.acceptance_time_fence,"exact submitted queue and fence published");
    queue_test::Wait(slot.acceptance_time_queue->fence,slot.acceptance_time_fence);
    nr_acceptance_gpu::Collect(slot);
    nr_acceptance_gpu::Sample sample{};
    Check(nr_acceptance_gpu::Pop(sample),"actual completed WARP GPU query yields Sample");
    Check(sample.config==g_config_generation.load()&&sample.source==nr_source_frame::Source{41,51,1}&&
          sample.instance==501&&sample.frame==1&&sample.since_reset==64&&sample.ms>0&&
          sample.queue==slot.acceptance_time_queue->identity&&sample.fence==slot.acceptance_time_fence,
          "Sample retains exact config/source/owner/queue/fence and positive GPU interval");
    nr_acceptance::performance_active.store(false);
    list.Detach();ReleaseCom(from);ReleaseCom(to);
    ReleaseCom(slot.acceptance_time_heap);ReleaseCom(slot.acceptance_time_readback);
    Stage("WarpTiming end");
}
struct EntryParams:NgxParams {
    std::map<std::string,double> numbers;
    std::map<std::string,void*> pointers;
    inline static void* table[16]{};
    static EntryParams& Of(const NgxParams*p){return *static_cast<EntryParams*>(const_cast<NgxParams*>(p));}
    template<class T> static NgxResult Number(const NgxParams*p,const char*k,T*v){
        const auto&n=Of(p).numbers;const auto i=n.find(k);if(i==n.end())return 0xBAD00005u;
        *v=T(i->second);return kNgxSuccess;}
    template<class T> static void Put(NgxParams*p,const char*k,T v){Of(p).numbers[k]=double(v);}
    static NgxResult Pointer(const NgxParams*p,const char*k,void**v){
        const auto&m=Of(p).pointers;const auto i=m.find(k);if(i==m.end())return 0xBAD00005u;
        *v=i->second;return kNgxSuccess;}
    static void PutPointer(NgxParams*p,const char*k,void*v){Of(p).pointers[k]=v;}
    EntryParams(){table[0]=table[1]=reinterpret_cast<void*>(&PutPointer);
        table[3]=reinterpret_cast<void*>(&Put<int>);table[4]=reinterpret_cast<void*>(&Put<unsigned>);
        table[6]=reinterpret_cast<void*>(&Put<float>);table[8]=table[9]=reinterpret_cast<void*>(&Pointer);
        table[11]=reinterpret_cast<void*>(&Number<int>);table[12]=reinterpret_cast<void*>(&Number<unsigned>);
        table[14]=reinterpret_cast<void*>(&Number<float>);vtable=table;}
};
ID3D12Device* entry_device=nullptr;ID3D12Resource* entry_output=nullptr;
unsigned entry_create_calls=0,entry_native_calls=0;
std::vector<std::unique_ptr<post_output_test::ClearViews>> entry_views;
NgxResult EntryAllocate(NgxParams**p){*p=new EntryParams;return kNgxSuccess;}
NgxResult EntryDestroy(NgxParams*p){delete static_cast<EntryParams*>(p);return kNgxSuccess;}
NgxResult EntryCreate(void*,ID3D12GraphicsCommandList*,int feature,void*,void**out){
    ++entry_create_calls;Check(feature==18,"entry stub only replaces opaque NR model");*out=new NgxHandle{901};return kNgxSuccess;}
NgxResult EntryRelease(void*,void*p){delete static_cast<NgxHandle*>(p);return kNgxSuccess;}
NgxResult EntryEvaluate(void*,ID3D12GraphicsCommandList*l,const void*,const void*raw,void*){
    ID3D12Resource*r=nullptr;Check(GetResource(static_cast<const NgxParams*>(raw),"DLSSNR.Output",&r)&&r,
        "entry model uses actual production-bound output");
    entry_views.push_back(std::make_unique<post_output_test::ClearViews>(entry_device));
    const float value[]={.55f,.5f,.45f,1};entry_views.back()->Clear(entry_device,l,r,0,value);
    return kNgxSuccess;
}
NgxResult EntryNative(ID3D12GraphicsCommandList*l,const NgxHandle*,const NgxParams*,void*){
    ++entry_native_calls;
    entry_views.push_back(std::make_unique<post_output_test::ClearViews>(entry_device));
    const float value[]={.25f,.375f,.5f,1};entry_views.back()->Clear(entry_device,l,entry_output,0,value);
    return kNgxSuccess;
}
NgxResult EntryNoop(ID3D12GraphicsCommandList*,const NgxHandle*,const NgxParams*,void*){
    return kNgxSuccess;
}
void WarpRrEntry(bool sr=false,bool pressure=false){
    Stage(sr?"WarpSrEntry begin":"WarpRrEntry begin");
    queue_test::Fixture f;entry_device=f.device;
    GetCurrentDirectoryW(MAX_PATH,g_log_path);wcscat_s(g_log_path,L"\\entry-rr.log");
    g_original_command_list_reset[0]=[](ID3D12GraphicsCommandList*l,ID3D12CommandAllocator*a,
        ID3D12PipelineState*p)->HRESULT{return ID3D12GraphicsCommandList_Reset(l,a,p);};
    g_config=ChainConfig{};g_config.enabled=true;g_config.mode=1;
    g_config.processing_start=nr_processing::Start::After;g_config.processing_selection=1;
    g_config.post_transfer_strength=1;g_config.warmup_after_create=0;g_config.suppress_reset_frame=false;
    nr_processing_runtime::Reconcile();nr_post_settings::bits.store(100u|256u);
    g_nr_api.initialized=true;g_nr_api.allocate=EntryAllocate;g_nr_api.destroy=EntryDestroy;
    g_nr_api.bridge_create=EntryCreate;g_nr_api.bridge_evaluate=EntryEvaluate;
    g_nr_api.bridge_release=EntryRelease;
#if defined(NR_CREATE_COMMIT_TEST)
    if(pressure){
        g_nr_create_commit_test_valid.store(1);
        g_nr_create_commit_test_available.store(75ull<<20);
        g_nr_create_commit_test_queries.store(0);
    }
#else
    Check(!pressure,"pressure case requires exact Core test injection");
#endif
    entry_create_calls=entry_native_calls=0;
    if(pressure)g_last_nr_create_result.store(0x12345678u);
    Stage("WarpEntry EnsureCodec begin");
    Check(EnsureCodec(f.device),"entry uses production GPU codec");
    Stage("WarpEntry EnsureCodec end");
    constexpr unsigned w=128,h=128;ID3D12Resource *color=nullptr,*depth=nullptr,*motion=nullptr;
    Check(CreateTexture(f.device,w,h,L"entry B",&color)&&CreateTexture(f.device,w,h,L"entry native Output",&entry_output)&&
          CreateTexture(f.device,w,h,L"entry depth",&depth,DXGI_FORMAT_R32_FLOAT)&&
          CreateTexture(f.device,w,h,L"entry MV",&motion,DXGI_FORMAT_R16G16_FLOAT),"typed RR inputs");
    D3D12_RESOURCE_STATES cs=D3D12_RESOURCE_STATE_COMMON,ds=cs,ms=cs;
    {queue_test::List init(f);post_output_test::ClearViews a(f.device),b(f.device);
        const float value[]={.25f,.375f,.5f,1},one[]={1,1,1,1},zero[]={0,0,0,0};
        Transition(init.native,color,cs,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);a.Clear(f.device,init.native,color,0,value);
        Transition(init.native,entry_output,cs,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);a.Clear(f.device,init.native,entry_output,1,value);
        Transition(init.native,depth,ds,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);a.Clear(f.device,init.native,depth,2,one);
        Transition(init.native,motion,ms,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);b.Clear(f.device,init.native,motion,0,zero);
        Transition(init.native,color,cs,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(init.native,depth,ds,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(init.native,motion,ms,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        post_output_test::Submit(f,init);}
    EntryParams params;SetResource(&params,"Color",color);SetResource(&params,"Output",entry_output);
    SetResource(&params,"Depth",depth);SetResource(&params,"MotionVectors",motion);
    SetUInt(&params,"DLSS.Render.Subrect.Dimensions.Width",w);
    SetUInt(&params,"DLSS.Render.Subrect.Dimensions.Height",h);
    SetFloat(&params,"MV.Scale.X",1);SetFloat(&params,"MV.Scale.Y",1);
    SetFloat(&params,"Jitter.Offset.X",0);SetFloat(&params,"Jitter.Offset.Y",0);
    SetFloat(&params,"DLSS.Pre.Exposure",1);SetFloat(&params,"DLSS.Exposure.Scale",1);
    SetInt(&params,"Reset",0);
    NgxHandle handle{501};FeatureRecord feature{};feature.handle=&handle;feature.feature_id=sr?1:13;
    feature.width=feature.out_width=w;feature.height=feature.out_height=h;
    feature.create_flags_known=feature.create_contract_known=true;feature.rr_depth_type=1;
    PublishObservedFeature(feature);feature=FindFeature(&handle);
    Check(feature.generation&&feature.feature_id==(sr?1:13),"real feature registry owns native identity");
    queue_test::List list(f);list.Close();list.Prime(f.a);list.Prime(f.a);f.Drain(f.a);
    CommandStateMetaContract meta{};meta.device_identity=g_next_command_device_identity.fetch_add(1);
    ID3D12Device5* extended=nullptr;
    const HRESULT query=ID3D12Device_QueryInterface(f.device,IID_ID3D12Device5,
        reinterpret_cast<void**>(&extended));
    if(query==E_NOINTERFACE)meta.known=true;
    else if(SUCCEEDED(query)&&extended){
        UINT count=0;
        if(SUCCEEDED(ID3D12Device5_EnumerateMetaCommands(extended,&count,nullptr))&&count<=256){
            std::vector<D3D12_META_COMMAND_DESC> descriptions(count);UINT actual=count;
            meta.known=count==0||SUCCEEDED(ID3D12Device5_EnumerateMetaCommands(extended,&actual,descriptions.data()));
            if(actual>count)meta.known=false;
            if(meta.known)for(UINT i=0;i<actual;++i){
                meta.initialization_dirty|=UINT(descriptions[i].InitializationDirtyState);
                meta.execution_dirty|=UINT(descriptions[i].ExecutionDirtyState);
            }
        }
        ReleaseCom(extended);
    }
    Check(meta.known&&((meta.initialization_dirty|meta.execution_dirty)&~kCommandStateCoveredDomains)==0,
        "actual WARP device meta-command state can be restored");
    queue_test::Hr(ID3D12Device_SetPrivateData(f.device,kCommandStateMetaContract,sizeof(meta),&meta),
        "entry device creation identity");
    CommandCreationInfo creation{};creation.serial=1;creation.device_identity=meta.device_identity;creation.method=1;
    Check(RegisterCreatedCommandStateList(list.native,meta,creation),"entry native list registered");
    Check(MH_Initialize()==MH_OK,"entry native command state hook initialization");
    Check(InstallNativeCommandStateHooks(list.native),"entry native command state hooks");
    auto state_probe=std::make_unique<NativeCommandState>();
    ID3D12GraphicsCommandList* observed=nullptr;
    (void)CaptureNativeCommandState(list.native,*state_probe,&observed);
    ReleaseNativeCommandState(*state_probe);state_probe.reset();
    const auto state_token=ReadCommandStateToken(list.native);
    Check(state_token!=0,"registered list has native state token");
    CompleteCommandStateInstallation(state_token,list.native,true,false);
    queue_test::Hr(ProcessCommandListReset(0,list.native,list.allocator,nullptr),"entry native Reset");
    ID3D12GraphicsCommandList_SetPipelineState(list.native,g_codec.encode);

    nr_acceptance::performance_active.store(true);
    nr_acceptance_gpu::read_index=nr_acceptance_gpu::write_index=0;
    const auto entry_kind=sr?nr_acceptance_gpu::Diagnostic::SrEntry:nr_acceptance_gpu::Diagnostic::RrEntry;
    const auto reached_kind=sr?nr_acceptance_gpu::Diagnostic::SrPrepareReached:
        nr_acceptance_gpu::Diagnostic::RrPrepareReached;
    const auto entry_before=nr_acceptance_gpu::diagnostics[static_cast<unsigned>(entry_kind)].load();
    const auto reached_before=nr_acceptance_gpu::diagnostics[static_cast<unsigned>(reached_kind)].load();
    {
        nr_source_runtime::StreamlineScope token(nr_source_frame::Source{41,51,1});
        nr_source_runtime::EvaluateScope source(list.native,&handle,&params,feature);
        Check(bool(nr_source_runtime::CurrentSource()),"entry source observed");
        const auto status=sr?ProcessEvaluate(EntryNative,list.native,&handle,&params,nullptr):
            ProcessRrPost(EntryNative,list.native,&handle,&params,nullptr,feature);
        Check(status==kNgxSuccess,"real native SR/RR entry succeeds");
        source.Finish(status);
    }
    if(pressure){
        Check(entry_native_calls==1&&entry_create_calls==0,
            "pressure preserves exactly one native SR/RR call and skips opaque NR Create");
        Check(g_nr_create_commit_pressure_declines.load()==1&&
              g_last_bypass_reason.load()==int(BypassReason::CommitHeadroomPressure),
            "actual entry records pressure admission, not vendor failure");
#if defined(NR_CREATE_COMMIT_TEST)
        Check(g_nr_create_commit_test_queries.load()==1&&
              g_last_nr_create_result.load()==0x12345678u,
            "cold-create pressure sampled once and did not fabricate a vendor result");
#endif
        bool retry=false;
        for(const auto& c:g_contexts)if(c.sr_handle==&handle&&c.nr_handle==nullptr&&
            c.next_create_retry_frame==c.frames_seen+300&&c.create_failures==0)retry=true;
        Check(retry,"actual entry retains the 300-frame cold-create retry contract");
        Check(!sr?nr_post_settings::rr_recorded.load()==0:true,
            "pressure decline does not claim post-RR NR recording");
        list.Close();list.Submit(f.a);f.Drain(f.a);
        D3D12_RESOURCE_STATES output_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        const auto native=post_output_test::ReadTexture(f,entry_output,output_state);
        const unsigned char native_pixel[]={0x00,0x34,0x00,0x36,0x00,0x38,0x00,0x3c};
        Check(native.size()>=sizeof(native_pixel)&&
              std::memcmp(native.data(),native_pixel,sizeof(native_pixel))==0,
            "actual game Output retains the native SR/RR FP16 pixel after pressure bypass");
#if defined(NR_CREATE_COMMIT_TEST)
        g_nr_create_commit_test_valid.store(-1);
#endif
        nr_acceptance::performance_active.store(false);list.Detach();entry_views.clear();
        ReleaseCom(color);ReleaseCom(depth);ReleaseCom(motion);ReleaseCom(entry_output);entry_output=nullptr;
        return;
    }
    std::printf("entry RR stage=%d recorded=%llu before=%llu prepare=%llu after=%llu finish=%llu bypass=%u state=%s\n",
        nr_post_settings::last_rr_stage.load(),nr_post_settings::rr_recorded.load(),
        nr_post_settings::rr_before_state_rejected.load(),nr_post_settings::rr_prepare_declined.load(),
        nr_post_settings::rr_after_state_rejected.load(),nr_post_settings::rr_finish_declined.load(),
        unsigned(g_last_bypass_reason.load()),SnapshotCommandStateEvidence().reason);
    if(sr)Check(g_nr_successful_evaluations.load()>0,"real SR entry reaches post NR finish");
    else Check(nr_post_settings::rr_recorded.load()>0,"real RR entry reaches post NR finish");
    Check(nr_acceptance_gpu::diagnostics[static_cast<unsigned>(entry_kind)].load()==entry_before+1&&
          nr_acceptance_gpu::diagnostics[static_cast<unsigned>(reached_kind)].load()==reached_before+1,
          "production entry and Prepare are independently observed in active session");
    list.Close();list.Submit(f.a);f.Drain(f.a);
    NrSlot* slot=nullptr;for(auto&c:g_contexts)for(auto&candidate:c.slots)
        if(candidate.acceptance_time_generation==g_config_generation.load()&&candidate.acceptance_time_source.sequence==1)
            slot=&candidate;
    Check(slot&&slot->acceptance_time_mask==15&&slot->acceptance_time_queue&&slot->acceptance_time_fence,
        sr?"real SR entry emits q0-q3 and publishes exact queue/fence":
           "real RR entry emits q0-q3 and publishes exact queue/fence");
    nr_acceptance_gpu::Collect(*slot);nr_acceptance_gpu::Sample sample{};
    Check(nr_acceptance_gpu::Pop(sample),"real SR/RR entry yields completed GPU query sample");
    Check(sample.source==nr_source_frame::Source{41,51,1}&&sample.frame&&sample.instance&&sample.ms>0&&
          sample.queue==slot->acceptance_time_queue->identity&&sample.fence==slot->acceptance_time_fence,
          "production identity and exact queue/fence survive native entry");
    {
        queue_test::List declined(f);
        const auto pre=static_cast<unsigned>(sr?nr_acceptance_gpu::Diagnostic::SrPreLifecycle:
            nr_acceptance_gpu::Diagnostic::RrPreLifecycle);
        const auto entries=nr_acceptance_gpu::diagnostics[static_cast<unsigned>(entry_kind)].load();
        const auto prepares=nr_acceptance_gpu::diagnostics[static_cast<unsigned>(reached_kind)].load();
        const auto declines=nr_acceptance_gpu::diagnostics[pre].load();
        const bool old_uncertain=g_lifecycle_tracking_uncertain.exchange(true);
        const auto declined_status=sr?ProcessEvaluate(EntryNoop,declined.native,&handle,&params,nullptr):
            ProcessRrPost(EntryNoop,declined.native,&handle,&params,nullptr,feature);
        Check(declined_status==kNgxSuccess,"real SR/RR entry can safely bypass on uncertain lifecycle");
        g_lifecycle_tracking_uncertain.store(old_uncertain);
        Check(nr_acceptance_gpu::diagnostics[static_cast<unsigned>(entry_kind)].load()==entries+1&&
              nr_acceptance_gpu::diagnostics[pre].load()==declines+1&&
              nr_acceptance_gpu::diagnostics[static_cast<unsigned>(reached_kind)].load()==prepares&&
              nr_acceptance_gpu::read_index==nr_acceptance_gpu::write_index,
              "production pre-Prepare lifecycle decline is diagnosed without a GPU sample");
        nr_acceptance::performance_active.store(false);
        const auto off_entries=nr_acceptance_gpu::diagnostics[static_cast<unsigned>(entry_kind)].load();
        const auto off_declines=nr_acceptance_gpu::diagnostics[pre].load();
        g_lifecycle_tracking_uncertain.store(true);
        const auto off_status=sr?ProcessEvaluate(EntryNoop,declined.native,&handle,&params,nullptr):
            ProcessRrPost(EntryNoop,declined.native,&handle,&params,nullptr,feature);
        Check(off_status==kNgxSuccess,"real SR/RR bypass remains safe with diagnostic session off");
        g_lifecycle_tracking_uncertain.store(old_uncertain);
        Check(nr_acceptance_gpu::diagnostics[static_cast<unsigned>(entry_kind)].load()==off_entries&&
              nr_acceptance_gpu::diagnostics[pre].load()==off_declines,
              "closed diagnostic session does not count ordinary SR/RR bypasses");
    }
    nr_acceptance::performance_active.store(false);list.Detach();entry_views.clear();
    ReleaseCom(color);ReleaseCom(depth);ReleaseCom(motion);ReleaseCom(entry_output);entry_output=nullptr;
}
void RunSr(){WarpRrEntry(true);std::printf("actual SR entry: %u checks; no game/model claim\n",queue_test::checks);}
void RunPressure(bool sr){WarpRrEntry(sr,true);std::printf("pressure %s entry: %u checks; no game/model claim\n",sr?"SR":"RR",queue_test::checks);}
void RunSecondaryPressure(){
#if defined(NR_CREATE_COMMIT_TEST)
    queue_test::Fixture f;
    auto primary_owner=std::make_unique<NrContext>();
    auto aux_owner=std::make_unique<NrContext>();
    auto& primary=*primary_owner;auto& aux=*aux_owner;
    auto& main=primary.slots[0];auto& slot=aux.slots[0];
    g_config=ChainConfig{};g_config.warmup_after_create=0;
    primary.instance_id=11;primary.frames_seen=100;
    primary.sr_handle=reinterpret_cast<const NgxHandle*>(501);
    primary.nr_handle=reinterpret_cast<NgxHandle*>(502);
    primary.mode=1;primary.width=primary.height=128;
    primary.successful_evaluations_since_reset=1;
    aux.layer_index=2;
    nr_dual_runtime::LayerPrepared prepared{};
    prepared.context=&aux;prepared.slot=&slot;prepared.source=&main;
    prepared.plan=nr_dual::MakePlan({2,{1,1}},{128,128},{{1,1},{16384,16384},{1,1}});
    auto binding=std::make_unique<NrBindingPacket>();
    prepared.binding=binding.get();
    prepared.full=true;
    Check(bool(prepared),"independent second layer prepared");
    ID3D12Resource* output=nullptr;
    Check(CreateTexture(f.device,128,128,L"recorded preceding layer",&output),
          "preceding layer output exists");
    D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
    queue_test::List list(f);post_output_test::ClearViews views(f.device);
    list.Close();list.Prime(f.a);list.Prime(f.a);f.Drain(f.a);
    list.ResetWithNewAllocator(f);
    Check(FindOrCreateLifecycleQueueLocked(f.a)!=nullptr,
          "preceding NR layer has an exact submission queue");
    Check(RecordNrUseLocked(list.native,primary,main),
          "preceding NR layer owns an actual command-list token");
    const auto token=primary.pending_recording_token;
    Check(token&&main.recording_command_lists==1,
          "recorded preceding layer retains a real lifetime closure");
    const float value[]={.25f,.375f,.5f,1};
    Transition(list.native,output,state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    views.Clear(f.device,list.native,output,0,value);
    g_nr_create_commit_test_valid.store(1);
    g_nr_create_commit_test_available.store(474750976ull);
    g_nr_create_commit_test_queries.store(0);
    g_last_nr_create_result.store(0x12345678u);
    const auto before=nr_dual_runtime::second_creates.load();
    FeatureRecord feature{};ColorContract color{};
    Check(nr_dual_runtime::RunOne(list.native,primary,main,prepared,feature,color)==0,
          "severe pressure defers second layer without fatal result");
    Check(nr_dual_runtime::second_creates.load()==before&&
          g_nr_create_commit_test_queries.load()==1&&
          g_last_nr_create_result.load()==0x12345678u,
          "no opaque Create or fabricated vendor failure");
    Check(!aux.nr_handle&&!aux.pending_recording_token&&
          !slot.recording_command_lists&&primary.pending_recording_token==token&&
          main.recording_command_lists==1&&!g_lifecycle_tracking_uncertain.load()&&
          !nr_dual_runtime::fatal_result.load()&&
          aux.dual_commit_retry_primary_frame==400&&
          aux.dual_commit_retry_config_generation==g_config_generation.load(),
          "deferred layer has no recorded dependencies or process isolation");
    list.Close();list.Submit(f.a);f.Drain(f.a);
    Check(!primary.pending_recording_token&&primary.completion_fence_value&&
          IsContextFenceCompleteLocked(primary)&&main.recording_command_lists==1&&
          !g_lifecycle_tracking_uncertain.load(),
          "preceding NR layer received actual submission and completed fence");
    const auto pixels=post_output_test::ReadTexture(f,output,state);
    const unsigned char expected[]={0x00,0x34,0x00,0x36,0x00,0x38,0x00,0x3c};
    Check(pixels.size()>=sizeof(expected)&&
          std::memcmp(pixels.data(),expected,sizeof(expected))==0,
          "earlier same-list GPU work submitted and completed unchanged");
    g_nr_create_commit_test_available.store(2ull<<30);
    primary.frames_seen=101;
    Check(nr_dual_runtime::DeferSecondaryCreateForCommitPressure(primary,aux)&&
          g_nr_create_commit_test_queries.load()==1,
          "cooldown avoids repeated create admission reads");
    primary.frames_seen=400;
    Check(!nr_dual_runtime::DeferSecondaryCreateForCommitPressure(primary,aux)&&
          g_nr_create_commit_test_queries.load()==2,
          "primary-frame retry admits recovered commit headroom");
    g_nr_create_commit_test_available.store(474750976ull);
    Check(nr_dual_runtime::DeferSecondaryCreateForCommitPressure(primary,aux),
          "renewed pressure remains a nonfatal deferral");
    const auto config=g_config_generation.fetch_add(1)+1;
    g_nr_create_commit_test_available.store(2ull<<30);
    Check(!nr_dual_runtime::DeferSecondaryCreateForCommitPressure(primary,aux)&&
          aux.dual_commit_retry_config_generation!=config,
          "settings change retries without waiting for old deadline");
    g_nr_create_commit_test_valid.store(-1);
    list.Detach();
    Check(!main.recording_command_lists&&!primary.pending_recording_token&&
          !g_lifecycle_tracking_uncertain.load(),
          "completed preceding layer releases its command-list ownership");
    ReleaseCom(output);
    // Exercise the actual auxiliary Create boundary after commit headroom
    // recovers, using the same complete-Core WARP guide and spatial path.
    g_config.enabled=true;g_config.mode=1;g_config.nr_passes=2;
    g_nr_api.initialized=true;g_nr_api.allocate=EntryAllocate;g_nr_api.destroy=EntryDestroy;
    g_nr_api.bridge_create=EntryCreate;g_nr_api.bridge_evaluate=EntryEvaluate;
    g_nr_api.bridge_release=EntryRelease;
    entry_device=f.device;entry_create_calls=0;
    Check(EnsureCodec(f.device),"secondary recovery has production codec");
    ID3D12Resource *neural=nullptr,*depth=nullptr,*motion=nullptr;
    Check(CreateTexture(f.device,128,128,L"secondary source",&neural)&&
          CreateTexture(f.device,128,128,L"secondary depth",&depth,DXGI_FORMAT_R32_FLOAT)&&
          CreateTexture(f.device,128,128,L"secondary motion",&motion,DXGI_FORMAT_R16G16_FLOAT),
          "secondary recovery has typed WARP inputs");
    D3D12_RESOURCE_STATES ns=D3D12_RESOURCE_STATE_COMMON,ds=ns,ms=ns;
    {queue_test::List init(f);post_output_test::ClearViews colors(f.device),motion_views(f.device);
        const float flat[]={.25f,.375f,.5f,1},one[]={1,1,1,1},zero[]={0,0,0,0};
        Transition(init.native,neural,ns,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        colors.Clear(f.device,init.native,neural,0,flat);
        Transition(init.native,depth,ds,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        colors.Clear(f.device,init.native,depth,1,one);
        Transition(init.native,motion,ms,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        motion_views.Clear(f.device,init.native,motion,0,zero);
        Transition(init.native,neural,ns,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(init.native,depth,ds,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(init.native,motion,ms,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        post_output_test::Submit(f,init);}
    EntryParams inputs,primary_params;
    SetResource(&inputs,"Depth",depth);SetResource(&inputs,"MotionVectors",motion);
    SetResource(&inputs,"Output",neural);
    SetUInt(&inputs,"DLSS.Render.Subrect.Dimensions.Width",128);
    SetUInt(&inputs,"DLSS.Render.Subrect.Dimensions.Height",128);
    SetFloat(&inputs,"MV.Scale.X",1);SetFloat(&inputs,"MV.Scale.Y",1);
    SetFloat(&inputs,"Jitter.Offset.X",0);SetFloat(&inputs,"Jitter.Offset.Y",0);
    SetFloat(&inputs,"DLSS.Pre.Exposure",1);SetFloat(&inputs,"DLSS.Exposure.Scale",1);
    SetInt(&inputs,"Reset",0);
    main.neural=neural;main.neural_state=ns;
    primary.params=&primary_params;
    FeatureRecord actual{};actual.handle=primary.sr_handle;actual.feature_id=13;
    actual.generation=1;actual.width=actual.height=128;
    actual.out_width=actual.out_height=128;actual.rr_depth_type=1;
    actual.create_flags_known=actual.create_contract_known=true;
    queue_test::List recovered(f);
    recovered.Close();recovered.Prime(f.a);recovered.Prime(f.a);f.Drain(f.a);
    recovered.ResetWithNewAllocator(f);
    g_nr_create_commit_test_valid.store(1);
    g_nr_create_commit_test_available.store(2ull<<30);
    const auto recovery_queries=g_nr_create_commit_test_queries.load();
    auto stage=nr_dual_runtime::PrepareOne(f.device,recovered.native,primary,main,
        &inputs,actual,128,128,neural,0,0,CodecColorMode::Automatic);
    Check(bool(stage),"normal margin prepares a real secondary layer");
    nr_dual_runtime::SealOne(recovered.native,main,stage);
    Check(stage.binding,"normal margin seals a real descriptor packet");
    const auto create_before=nr_dual_runtime::second_creates.load();
    Check(nr_dual_runtime::RunOne(recovered.native,primary,main,stage,actual,ColorContract{})==0&&
          nr_dual_runtime::second_creates.load()==create_before+1&&
          entry_create_calls==1&&stage.context->nr_handle&&!g_lifecycle_tracking_uncertain.load()&&
          g_nr_create_commit_test_queries.load()==recovery_queries+1,
          "injected 2 GiB margin reaches one actual auxiliary Create callback");
    // This synthetic primary has no actual prior Feature18 Evaluate output.
    // Do not submit its spatial work as a substitute for a real frame. An
    // unsubmitted Create recording owns resources, so explicitly quarantine
    // its closure until this isolated test process exits.
    unsigned long long recovery_token=0;
    Check(ReadLifecycleToken(recovered.native,recovery_token)&&recovery_token&&
          QuarantineUnsubmittedLifecycleRecordLocked(recovery_token,"synthetic recovery fixture")&&
          g_lifecycle_tracking_uncertain.load()&&stage.context->lifecycle_dependencies_quarantined,
          "unsubmitted synthetic Create keeps the complete dependency closure");
    recovered.Detach();
    Check(!stage.context->pending_recording_token&&
          !stage.context->recording_command_lists&&!stage.slot->recording_command_lists&&
          !FindLifecycleCommandListLocked(recovery_token),
          "synthetic list detach drops recording references without releasing quarantined resources");
    ReleaseCom(depth);ReleaseCom(motion);
    std::printf("secondary commit pressure: %u checks; real WARP preceding fence and stub Create boundary, no recovered GPU/game claim\n",queue_test::checks);
#else
    Check(false,"secondary pressure requires complete Core test injection");
#endif
}
void RunPressureBounds(){
#if defined(NR_CREATE_COMMIT_TEST)
    static NrContext context{};
    context.frames_seen=41;
    g_nr_create_commit_test_valid.store(1);
    g_nr_create_commit_test_available.store(kMinimumNrCreateAvailableCommitBytes-1);
    Check(DeferNrCreateForCommitPressure(context)&&context.next_create_retry_frame==341,
        "below 1 GiB records pressure and 300-frame retry");
    context.next_create_retry_frame=0;
    g_nr_create_commit_test_available.store(kMinimumNrCreateAvailableCommitBytes);
    Check(!DeferNrCreateForCommitPressure(context)&&context.next_create_retry_frame==0,
        "exactly 1 GiB admits new model Create");
    g_nr_create_commit_test_available.store(kMinimumNrCreateAvailableCommitBytes+1);
    Check(!DeferNrCreateForCommitPressure(context)&&context.next_create_retry_frame==0,
        "above 1 GiB admits new model Create");
    g_nr_create_commit_test_valid.store(0);
    g_nr_create_commit_test_available.store(0);
    Check(!DeferNrCreateForCommitPressure(context)&&context.next_create_retry_frame==0,
        "unavailable system reading is not interpreted as zero commit");
    g_nr_create_commit_test_valid.store(-1);
    std::printf("commit pressure bounds: %u checks; no vendor/game claim\n",queue_test::checks);
#else
    Check(false,"policy-bound test requires exact Core test injection");
#endif
}
void RunNativeRouteObservation(){
    auto probe=std::make_unique<nr_acceptance::Session>();
    probe->program=nr_acceptance_policy::Program::Screenshots;
    probe->count=9;probe->index=0;
    const auto previous_config=g_config_generation.exchange(0);
    constexpr unsigned config=0;
    probe->groups[0].generation=config;
    probe->groups[0].requested.enabled=false; // Native baseline still has a route.
    nr_acceptance::session=probe.get();nr_acceptance::active.store(true);
    const bool was_enabled=g_enabled_fast.exchange(false);
    const auto old_sr=g_original_evaluate,old_rr=g_original_dlssd_evaluate;
    g_original_evaluate=EntryNoop;g_original_dlssd_evaluate=EntryNoop;
    NgxHandle sr{901},rr{902};
    FeatureRecord sr_record{};sr_record.handle=&sr;sr_record.feature_id=1;
    FeatureRecord rr_record{};rr_record.handle=&rr;rr_record.feature_id=13;
    {ScopedNgxLayer layer(NgxLayer::Dlss);PublishObservedFeature(sr_record);}
    {ScopedNgxLayer layer(NgxLayer::Dlssd);PublishObservedFeature(rr_record);}
    Check(HookEvaluate(nullptr,&sr,nullptr,nullptr)==kNgxSuccess,
        "NR-off baseline reaches a real game Feature1 Evaluate hook");
    Check(probe->groups[0].native_sr_entries==1&&probe->groups[0].native_rr_entries==0&&
          std::strcmp(nr_acceptance::NativeRouteName(probe->groups[0]),"native-SR")==0,
        "NR-off baseline identifies native SR from entry, not RR policy");
    probe->index=1;probe->groups[1].generation=config;
    Check(HookDlssdEvaluate(nullptr,&rr,nullptr,nullptr)==kNgxSuccess&&
          probe->groups[1].native_rr_entries==1&&probe->groups[0].native_rr_entries==0&&
          std::strcmp(nr_acceptance::NativeRouteName(probe->groups[1]),"native-RR")==0,
        "new group records only its own real Feature13 entry");
    Check(HookEvaluate(nullptr,&sr,nullptr,nullptr)==kNgxSuccess&&
          std::strcmp(nr_acceptance::NativeRouteName(probe->groups[1]),"mixed")==0,
        "one group with both actual native features is mixed");
    const auto mixed_sr=probe->groups[1].native_sr_entries;
    g_inside_ngx_evaluate=true;
    Check(HookEvaluate(nullptr,&sr,nullptr,nullptr)==kNgxSuccess&&
          probe->groups[1].native_sr_entries==mixed_sr,
        "forwarded nested Feature1 wrapper is not counted as a second game call");
    g_inside_ngx_evaluate=false;
    probe->index=2;probe->groups[2].generation=config+1;
    Check(HookEvaluate(nullptr,&sr,nullptr,nullptr)==kNgxSuccess&&
          std::strcmp(nr_acceptance::NativeRouteName(probe->groups[2]),"unknown")==0,
        "mismatched configuration cannot borrow a previous group's route");
    Check(nr_acceptance_gpu::read_index==nr_acceptance_gpu::write_index,
        "native route observation does not fabricate a GPU timing Sample");
    g_original_evaluate=old_sr;g_original_dlssd_evaluate=old_rr;
    g_enabled_fast.store(was_enabled);nr_acceptance::active.store(false);
    nr_acceptance::session=nullptr;
    g_config_generation.store(previous_config);
}
void Run(){
    Stage("Run begin");
    InitializeCriticalSection(&g_chain_cs);
    RunNativeRouteObservation();
    wchar_t full[MAX_PATH]{};GetFullPathNameW(L"acceptance-user.ini",MAX_PATH,full,nullptr);wcscpy_s(g_ini_path,full);
    const std::string sentinel="[NRBeforeSR]\r\nEnabled=1\r\nOwner=unchanged\r\n";
    {std::ofstream out("acceptance-user.ini",std::ios::binary);out<<sentinel;}
    ChainConfig original=SnapshotConfig();original.enabled=true;original.nr_passes=2;
    original.processing_start=nr_processing::Start::After;original.mode=1;
    original.temporal_backend=0;original.region_preview=0;original.light_preview=0;
    original.face_preview=0;original.colour_neutral_probe=false;
    ApplyConfig(original,"controller fixture",false);original=SnapshotConfig();
    // Normal startup migrations precede acceptance. Freeze bytes only after
    // that real initialization; this controller must add no further writes.
    std::ifstream initial("acceptance-user.ini",std::ios::binary);
    const std::string before_acceptance((std::istreambuf_iterator<char>(initial)),{});initial.close();
    nr_processing_runtime::Reconcile();nr_post_settings::bits.store(75u|256u);
    auto* runtime=reinterpret_cast<reshade::api::effect_runtime*>(std::uintptr_t(0x1000));
    Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Colour,false),"actual start takes transient ownership");
    Check(nr_f8::g_config_hold.load()&&nr_post_settings::Read().percent==100,"actual first scale applied under hold");
    Check(!nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Current,false),"second owner refused");
    Check(!nr_f8::RequestCore()&&!RequestStageCapture(),"normal captures cannot steal config owner");
    ChainConfig conflicting=original;conflicting.intensity+=0.1f;
    ApplyConfig(conflicting,"user during acceptance",true);
    Check(g_config.intensity==original.intensity,"external mutation held");
    AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    auto& s=*nr_acceptance::session;
    s.window.started=GetTickCount64()-3001;
    nr_reset_observation::counters.layers[0].last.config_generation=s.groups[0].generation;
    nr_reset_observation::counters.layers[0].last.since_reset=64;
    g_nr_successful_evaluations.fetch_add(64);
    nr_acceptance::Tick(runtime);
    Check(s.window.phase==nr_acceptance_policy::Phase::Warmup,"real controller rejects evaluations without submission");
    Check(s.groups[0].warmup_evaluations==64&&s.groups[0].warmup_submissions==0,"warmup activity separate from unstarted measurement");
    g_nr_transfer_submitted_frames.fetch_add(1);nr_acceptance::Tick(runtime);
    Check(s.window.phase==nr_acceptance_policy::Phase::Measure,"real adoption and warmup admit measurement");
    Check(s.groups[0].warmup_submissions==1,"warmup submission recorded even before measured interval");
    Check(!g_feedback_requested.load()&&StageCaptureStatus()==0,"no capture or report during measurement");
    // Exercise A->B->A through production request and normalization, not direct
    // config assignment. Only test time and upstream observation are synthetic.
    s.index=1;nr_acceptance::StartGroup(s);Check(nr_post_settings::Read().percent==99,"second scale applied");
    s.index=0;nr_acceptance::StartGroup(s);Check(nr_post_settings::Read().percent==100,"first scale reapplied");
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);
    nr_acceptance::Cancel();DrainExport();
    Check(!nr_f8::g_config_hold.load()&&!nr_acceptance::Active(),"cancel releases only transient owner");
    Check(nr_acceptance::SameRequested(original,SnapshotConfig())&&nr_post_settings::Read().percent==75,"original fields and separate post precision restored");
    Sleep(2);
    Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Current,false),"current baseline restarts");
    const auto history=g_history_generation.load();nr_acceptance::Cancel();DrainExport();
    Check(g_history_generation.load()==history,"unchanged baseline restore causes no gratuitous reset");
    // Exercise the real text publisher -> acceptance consumer seam. No GPU or
    // game evidence is fabricated: only report contents/config are test inputs.
    for(unsigned report_case=0;report_case<3;++report_case){
        Sleep(2);
        Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Current,false),"report pairing starts");
        AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
        auto& run=*nr_acceptance::session;auto& group=run.groups[0];
        Check(nr_core_feedback::Request(),"actual text-only report requested");
        group.report_requested=true;group.stage_tick=GetTickCount64();
        auto pair=nr_core_feedback::SnapshotPair();
        std::snprintf(group.report_session,sizeof(group.report_session),"%s",pair.session);
        AcquireSRWLockExclusive(&g_feedback_lock);
        nr_core_feedback::g_pair.config=group.generation+(report_case==1?1:0);
        ReleaseSRWLockExclusive(&g_feedback_lock);
        g_feedback_requested=false; // simulated worker claim, no fake render callback
        nr_core_feedback::nr_feedback_report::Output report{};
        report.text=std::make_shared<const std::string>("fixture TXT");
        report.path=L"fixture-report.txt";report.complete=report_case!=2;
        nr_bundle::Result archive{};
        nr_core_feedback::Publish(archive,false,"fixture",nullptr,&report,false);
        pair=nr_core_feedback::SnapshotPair();
        Check(!pair.complete&&pair.text_complete==(report_case!=2),"text and image completion remain distinct");
        run.window.phase=nr_acceptance_policy::Phase::Report;
        nr_acceptance::Tick(runtime);
        ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);
        DrainExport();
        FILE* receipt=_wfopen(nr_acceptance::last_path,L"rb");char contents[8192]{};
        Check(receipt!=nullptr,"paired acceptance receipt exists");
        std::fread(contents,1,sizeof(contents)-1,receipt);std::fclose(receipt);
        Check((std::strstr(contents,"paired-report-unavailable")!=nullptr)==(report_case!=0),"complete text accepted; wrong config and partial text refused");
    }
    Sleep(2);
    Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Temporal,false),"temporal comparison starts without changing layer count");
    Check(g_config.nr_passes==original.nr_passes,"test does not increase NR workload");
    AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    nr_acceptance::session->last_present=GetTickCount64()-5001;nr_acceptance::Tick(nullptr);
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);DrainExport();
    Check(!nr_acceptance::Active()&&!nr_f8::g_config_hold.load(),"missing Present restores without calling runtime");
    Sleep(2);
    Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::PrivateSr,false),"private SR comparison starts");
    Check(!g_config.private_residual_sr&&g_config.temporal_backend==0&&nr_post_settings::Read().percent==25,"low spatial baseline excludes external temporal");
    AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    auto& private_session=*nr_acceptance::session;
    private_session.index=1;nr_acceptance::StartGroup(private_session);
    Check(g_config.private_residual_sr&&g_config.temporal_backend==0&&nr_post_settings::Read().percent==25,"same low scale enables private SR only");
    private_session.index=2;nr_acceptance::StartGroup(private_session);
    Check(!g_config.private_residual_sr&&nr_post_settings::Read().percent==100,"high spatial baseline retains model and layer count");
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);
    nr_acceptance::Cancel();DrainExport();
    Check(nr_acceptance::SameRequested(original,SnapshotConfig())&&nr_post_settings::Read().percent==75,"private SR trial restores complete config and post precision");
    Sleep(2);
    Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Colour,false),"runtime destruction case starts");
    nr_acceptance::RuntimeDestroyed(runtime);DrainExport();
    g_last_carrier_feature.store(13);g_live_rr_handles.store(1);
    original.mode=2;original.processing_start=nr_processing::Start::Before;
    ApplyConfig(original,"RR observed fixture",false);nr_processing_runtime::Reconcile();
    const auto pre_scale=SnapshotConfig().custom_work_scale;
    Sleep(2);Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Colour,false),"observed RR starts despite saved pre preference");
    Check(nr_acceptance::session->original_effective==nr_processing::Start::After&&g_config.custom_work_scale==pre_scale&&nr_post_settings::Read().percent==100,"actual RR uses post precision without editing saved pre field");
    nr_acceptance::Cancel();DrainExport();g_last_carrier_feature.store(0);g_live_rr_handles.store(0);
    // A screenshot session must start from F6 OFF/Before without asking the
    // owner to enable NR first, and must restore every transient route field.
    ChainConfig off=SnapshotConfig();off.enabled=false;off.mode=0;
    off.processing_start=nr_processing::Start::Before;
    off.spatial_experiment_mode=2; // Saved new experiment must not contaminate nine existing baselines.
    ApplyConfig(off,"screenshot fixture",false);off=SnapshotConfig();
    const auto original_route=nr_processing_runtime::session;
    const auto original_post=nr_post_settings::Read();
    Sleep(2);Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Screenshots,false),"NR-off Before screenshot session starts");
    Check(!g_config.enabled&&g_config.processing_start==nr_processing::Start::After&&nr_acceptance::session->count==9&&
        g_config.spatial_experiment_mode==0,"native baseline temporarily routes After with both NR and spatial experiment off");
    Check(!nr_acceptance::PerformanceActive(),"native no-NR baseline has no plugin GPU instrumentation");
    AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    auto& pictures=*nr_acceptance::session;
    pictures.index=1;nr_acceptance::StartGroup(pictures);
    Check(nr_acceptance::PerformanceActive(),"NR screenshot group enables session-only GPU timestamps");
    Check(g_config.enabled&&g_config.processing_start==nr_processing::Start::After&&nr_post_settings::Read().percent==100&&
        g_config.spatial_experiment_mode==0,"next group enables NR and full post precision without spatial experiment");
    pictures.index=3;nr_acceptance::StartGroup(pictures);
    Check(g_config.private_residual_sr&&nr_post_settings::Read().percent==50&&g_config.nr_passes==off.nr_passes&&
        g_config.spatial_experiment_mode==0,"private-SR shot changes only route/precision/backend");
    pictures.index=8;nr_acceptance::StartGroup(pictures);
    Check(g_config.temporal_backend==2&&g_config.temporal_mode==1&&nr_post_settings::Read().percent==50&&
        g_config.spatial_experiment_mode==0,"AMDOF low-frequency shot uses same model/layers");
    auto& temporal_picture=pictures.groups[8];
    temporal_picture.screenshot_phase=nr_acceptance_policy::ScreenshotPhase::Capture;
    const auto saved_owner=g_applied_nr_identity.instance_id;
    g_applied_nr_identity.instance_id=321;
    nr_reset_observation::counters.layers[0].last.config_generation=temporal_picture.generation;
    nr_reset_observation::counters.layers[0].last.since_reset=64;
    g_nr_transfer_submitted_frames=temporal_picture.submit_start+64;
    nr_temporal_status::gpu_config=temporal_picture.generation;nr_temporal_status::gpu_instance=321;
    nr_temporal_status::gpu_frames=64;nr_temporal_status::gpu_adopted=100;
    pictures.window.started=GetTickCount64()-3001;
    nr_acceptance::TickScreenshotsAtExtent(runtime,1920,1080);
    Check(temporal_picture.adoption_probe&&!temporal_picture.shots[0].requested,"old adopted pixels only arm fresh-work probe");
    nr_temporal_status::gpu_frames.fetch_add(1);
    nr_acceptance::TickScreenshotsAtExtent(runtime,1920,1080);
    Check(!temporal_picture.shots[0].requested,"completed frames with no new adopted pixels cannot capture");
    nr_temporal_status::gpu_adopted.fetch_add(1);
    nr_acceptance::TickScreenshotsAtExtent(runtime,1920,1080);
    Check(temporal_picture.shots[0].requested,"fresh adopted work qualifies screenshot request");
    pictures.pending_shot[0]=0;g_applied_nr_identity.instance_id=saved_owner;
    pictures.index=2;nr_acceptance::StartGroup(pictures);
    auto& timed_picture=pictures.groups[2];
    nr_acceptance_gpu::read_index=nr_acceptance_gpu::write_index=0;
    nr_acceptance_gpu::Sample stale{};stale.config=timed_picture.generation-1;stale.ms=1;
    nr_acceptance_gpu::samples[nr_acceptance_gpu::write_index++]=stale;
    for(unsigned i=0;i<nr_acceptance_policy::kScreenshotWarmupSamples+
                    nr_acceptance_policy::kScreenshotGpuSamples;++i){
        nr_acceptance_gpu::Sample x{};x.config=timed_picture.generation;
        x.history=timed_picture.performance_history;x.since_reset=64;
        x.work_width=960;x.work_height=540;x.output_width=1920;x.output_height=1080;
        x.instance=500;x.frame=i+1;x.queue=700;x.fence=i+1;
        x.source={10,20,i+1};x.ms=2.0;
        nr_acceptance_gpu::samples[nr_acceptance_gpu::write_index++%nr_acceptance_gpu::kRing]=x;
    }
    Check(!nr_acceptance::TickScreenshotPerformance(pictures,timed_picture,runtime,GetTickCount64())&&
        timed_picture.performance_warmup==180&&timed_picture.performance_count==600&&
        timed_picture.screenshot_phase==nr_acceptance_policy::ScreenshotPhase::Capture,
        "one-click performance requires 180 owned warmup sources then 600 completed GPU samples");
    Check(!nr_acceptance::PerformanceActive(),"GPU instrumentation stops before image capture");
    // All callbacks may have completed before Present drains the ring. The
    // only observed adoption may be from warmup; completed GPU timing still
    // must not be represented as adoption by the 600 measurement samples.
    for(const unsigned group_index:{3u,8u}){
        pictures.index=group_index;nr_acceptance::StartGroup(pictures);
        auto& buffered=pictures.groups[group_index];
        nr_acceptance_gpu::read_index=nr_acceptance_gpu::write_index=0;
        const bool private_route=buffered.requested.private_residual_sr;
        Check(private_route||buffered.requested.temporal_backend,"buffered route has a GPU adoption requirement");
        for(unsigned i=0;i<nr_acceptance_policy::kScreenshotWarmupSamples+
                        nr_acceptance_policy::kScreenshotGpuSamples;++i){
            nr_acceptance_gpu::Sample x{};x.config=buffered.generation;
            x.history=buffered.performance_history;x.since_reset=64;
            x.work_width=960;x.work_height=540;x.output_width=1920;x.output_height=1080;
            x.instance=501+group_index;x.frame=i+1;x.queue=701+group_index;x.fence=i+1;
            x.source={11+group_index,21+group_index,i+1};x.ms=2.0;
            x.private_composed=private_route;x.temporal_recorded=!private_route;
            nr_acceptance_gpu::samples[nr_acceptance_gpu::write_index++%nr_acceptance_gpu::kRing]=x;
        }
        if(private_route)nr_private_sr_status::adopted.fetch_add(180);
        else {
            nr_temporal_status::gpu_config=buffered.generation;
            nr_temporal_status::gpu_instance=501+group_index;
            nr_temporal_status::gpu_adopted.fetch_add(180);
        }
        Check(!nr_acceptance::TickScreenshotPerformance(pictures,buffered,runtime,GetTickCount64())&&
            buffered.performance_warmup==180&&buffered.performance_count==600&&
            std::strcmp(buffered.performance_reason,"complete")==0,
            "buffered 180+600 samples complete timing with only warmup-period adoption observed");
    }
    // The production Reset hook invokes this only after native Reset succeeds.
    // An unsubmitted token can be reused; an already submitted slot remains
    // pinned to its exact retirement fence.
    {
        LifecycleCommandList* record=nullptr;
        for(auto& candidate:g_lifecycle_command_lists)if(!candidate.token){record=&candidate;break;}
        Check(record!=nullptr,"empty lifecycle record available for Reset regression");
        if(record){
            NrSlot timing_slot{};record->token=0xabcde;record->slots[0]=&timing_slot;record->slot_count=1;
            timing_slot.acceptance_time_token=record->token;timing_slot.acceptance_time_pending=true;
            timing_slot.acceptance_time_mask=15;
            nr_acceptance_gpu::DiscardUnsubmitted(record->token);
            Check(!timing_slot.acceptance_time_pending&&!timing_slot.acceptance_time_token&&
                !timing_slot.acceptance_time_mask,"successful Reset clears unsubmitted exact-token timing");
            timing_slot.acceptance_time_token=record->token;timing_slot.acceptance_time_pending=true;
            timing_slot.acceptance_time_fence=7;record->submission_count=1;
            nr_acceptance_gpu::DiscardUnsubmitted(record->token);
            Check(timing_slot.acceptance_time_pending&&timing_slot.acceptance_time_fence==7,
                "submitted Reset preserves timestamp until exact fence completion");
            *record={};
        }
    }
    pictures.index=0;nr_acceptance::StartGroup(pictures);
    auto& sg=pictures.groups[0];auto& sh=sg.shots[0];
    Check(!nr_acceptance::TickScreenshotPerformance(pictures,sg,runtime,GetTickCount64())&&
        std::strstr(sg.performance_reason,"not-applicable")!=nullptr,
        "NR-off baseline has no fabricated plugin GPU sample and reaches image phase");
    pictures.window.started=GetTickCount64()-3001;
    nr_acceptance::TickScreenshotsAtExtent(runtime,1920,1080);
    Check(sh.requested&&pictures.pending_shot[0],"actual screenshot branch schedules native baseline after warmup");
    char good[120]{};std::snprintf(good,sizeof(good),"DLSS5_SHOT_%llu_0_0.png",pictures.id);
    {std::ofstream image(good,std::ios::binary);image<<"test-image";}
    char absolute[MAX_PATH]{};GetFullPathNameA(good,MAX_PATH,absolute,nullptr);
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);
    nr_acceptance::ScreenshotReceipt(runtime,"C:\\wrong-token.png",1920,1080);
    Check(!sh.received&&!sg.reason[0],"wrong screenshot token ignored");
    nr_acceptance::ScreenshotReceipt(reinterpret_cast<reshade::api::effect_runtime*>(std::uintptr_t(0x2000)),absolute,1920,1080);
    Check(!sh.received,"wrong runtime callback ignored");
    nr_acceptance::ScreenshotReceipt(runtime,absolute,1280,720);
    Check(!sh.received&&sg.reason[0],"display resize rejects callback");
    sg.reason[0]=0;g_config_generation.fetch_add(1);
    nr_acceptance::ScreenshotReceipt(runtime,absolute,1920,1080);
    Check(!sh.received&&sg.reason[0],"configuration change rejects callback");
    g_config_generation.fetch_sub(1);
    sg.reason[0]=0;nr_acceptance::ScreenshotReceipt(runtime,absolute,1920,1080);
    Check(sh.received&&sg.shot_count==1,"exact callback and existing file accepted");
    // Starting an NR group with no real owner/submissions cannot request a shot.
    AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    pictures.index=1;nr_acceptance::StartGroup(pictures);pictures.groups[1].screenshot_phase=nr_acceptance_policy::ScreenshotPhase::Capture;
    pictures.window.started=GetTickCount64()-3001;
    nr_acceptance::TickScreenshotsAtExtent(runtime,1920,1080);
    Check(!pictures.groups[1].shots[0].requested,"zero NR adoption cannot schedule screenshot");
    pictures.groups[1].shots[0].requested=true;pictures.groups[1].shots[0].config=pictures.groups[1].generation;
    pictures.groups[1].shots[0].instance=g_applied_nr_identity.instance_id+1;
    pictures.groups[1].shots[0].width=1920;pictures.groups[1].shots[0].height=1080;
    char owner_tag[120]{};std::snprintf(owner_tag,sizeof(owner_tag),"DLSS5_SHOT_%llu_1_0.png",pictures.id);
    {std::ofstream image(owner_tag,std::ios::binary);image<<"test-image";}
    char owner_path[MAX_PATH]{};GetFullPathNameA(owner_tag,MAX_PATH,owner_path,nullptr);
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);
    nr_acceptance::ScreenshotReceipt(runtime,owner_path,1920,1080);
    Check(!pictures.groups[1].shots[0].received&&pictures.groups[1].reason[0],"wrong NR owner rejects callback");
    // A pending request without a callback is bounded; the watchdog path never
    // dereferences the opaque runtime pointer.
    pictures.groups[1].reason[0]=0;pictures.groups[1].shots[0].instance=g_applied_nr_identity.instance_id;
    pictures.groups[1].shots[0].tick=GetTickCount64()-5001;
    nr_acceptance::TickScreenshotsAtExtent(runtime,1920,1080);
    Check(std::strstr(pictures.groups[1].reason,"timeout")!=nullptr,"missing screenshot callback times out");
    // A cancelled measurement must preserve observations made before the
    // timestamp preparation stage, even when no GPU sample was collected.
    pictures.groups[1].screenshot_phase=nr_acceptance_policy::ScreenshotPhase::Warmup;
    pictures.groups[1].performance_reason[0]=0;
    const bool cancelled_rr=pictures.groups[1].post_rr;
    const auto cancelled_entry=cancelled_rr?nr_acceptance_gpu::Diagnostic::RrEntry:
        nr_acceptance_gpu::Diagnostic::SrEntry;
    nr_acceptance_gpu::diagnostics[static_cast<unsigned>(cancelled_entry)].fetch_add(1);
    nr_acceptance::Cancel();DrainExport();
    char export_path[MAX_PATH]{};
    WideCharToMultiByte(CP_UTF8,0,nr_acceptance::last_screenshot_page,-1,export_path,MAX_PATH,nullptr,nullptr);
    std::string manifest_path(export_path);
    const auto page=manifest_path.rfind("index.html");
    Check(page!=std::string::npos,"screenshot export page path available");
    if(page!=std::string::npos){
        manifest_path.replace(page,std::strlen("index.html"),"screenshots.json");
        std::ifstream exported(manifest_path,std::ios::binary);
        const std::string json((std::istreambuf_iterator<char>(exported)),{});
        Check(json.find("\"measurementAdoption\":\"unverified-not-bound-to-measured-samples\"")!=std::string::npos&&
            json.find("\"statusScope\":\"completed-fence-local-GPU-timing-only\"")!=std::string::npos&&
            json.find("\"screenshotFreshAdoption\":\"incomplete\"")!=std::string::npos&&
            json.find("\"entryStatus\":\"entry-not-observed\"")!=std::string::npos&&
            json.find("AdoptedDuringMeasure")==std::string::npos,
            "whole-group warmup adoption cannot be exported as measured-sample adoption");
        for(const unsigned group_index:{3u,8u}){
            const auto begin=json.find("\"group\":"+std::to_string(group_index)+",");
            const auto end=begin==std::string::npos?begin:json.find("\"shots\":[",begin);
            const auto record=begin==std::string::npos?std::string{}:json.substr(begin,end-begin);
            Check(record.find("\"status\":\"complete\"")!=std::string::npos&&
                record.find("\"validWarmup\":180")!=std::string::npos&&
                record.find("\"validGpuSamples\":600")!=std::string::npos&&
                record.find("\"measurementAdoption\":\"unverified-not-bound-to-measured-samples\"")!=std::string::npos,
                "temporal/private buffered timing complete is not measured-sample adoption proof");
        }
        const auto begin=json.find("\"group\":1,");
        const auto end=begin==std::string::npos?begin:json.find("\"shots\":[",begin);
        const auto record=begin==std::string::npos?std::string{}:json.substr(begin,end-begin);
        Check(record.find("\"status\":\"cancelled-partial\"")!=std::string::npos&&
            record.find("\"entryStatus\":\"entry-observed\"")!=std::string::npos&&
            record.find(cancelled_rr?"\"rr-entry\":1":"\"sr-entry\":1")!=std::string::npos,
            "cancelled pre-Prepare group preserves observed entry diagnostic and partial status");
    }
    Check(!nr_acceptance::PerformanceActive(),"cancel disables diagnostic GPU instrumentation");
    Check(nr_acceptance::SameRequested(off,SnapshotConfig())&&SnapshotConfig().spatial_experiment_mode==2&&
        nr_processing_runtime::session.Current()==original_route.Current()&&
        nr_post_settings::Read().percent==original_post.percent,"screenshot cancel restores NR-off Before and post precision");
    nr_acceptance::ScreenshotReceipt(runtime,absolute,1920,1080);
    Check(!nr_acceptance::Active(),"late screenshot cannot revive cancelled session");
    Sleep(2);Check(nr_acceptance::Start(runtime,nr_acceptance_policy::Program::Screenshots,false),"second screenshot session starts after cancel");
    AcquireSRWLockExclusive(&nr_acceptance::lock);AcquireSRWLockExclusive(&nr_f8::g_control);
    nr_acceptance::session->last_present=GetTickCount64()-5001;
    nr_acceptance::TickScreenshotsAtExtent(nullptr,0,0);
    ReleaseSRWLockExclusive(&nr_f8::g_control);ReleaseSRWLockExclusive(&nr_acceptance::lock);DrainExport();
    Check(!nr_acceptance::Active()&&nr_acceptance::SameRequested(off,SnapshotConfig()),"stopped Present restores screenshot session");
    std::ifstream in("acceptance-user.ini",std::ios::binary);std::string after((std::istreambuf_iterator<char>(in)),{});
    Check(after==before_acceptance,"user INI byte identical across all operations");
    Sleep(300); // let idle controller watchdogs observe their exact session id
    DeleteCriticalSection(&g_chain_cs);
    WarpTiming();
    WarpRrEntry();
    Stage("Run end");
    std::printf("actual acceptance controller: %u checks; no game/model or image-quality claim\n",queue_test::checks);
}
}
