// Actual Core consumers, immutable binding packets and Execute/Signal/Reset.
// Constructed NR1/NR2 surfaces: this is not NGX inference or a game test.
namespace temporal_core_test {
using queue_test::Check;using queue_test::Hr;
using post_output_test::Submit;using post_output_test::ReadTexture;using post_output_test::ClearViews;
template<class F> constexpr bool HasMotionSnapshotProof=std::is_invocable_v<F,ID3D12GraphicsCommandList*,NrContext&,NrSlot&,
    const NrGuideBinding&,const FeatureRecord&,unsigned,unsigned,unsigned,unsigned,bool>;
template<class F> nr_temporal::Prepared PrepareCompatible(F prepare,ID3D12GraphicsCommandList* list,NrContext& context,NrSlot& slot,
    const NrGuideBinding& guides,const FeatureRecord& feature,unsigned w,unsigned h,bool snapshot){
    // The old frozen Core must compile unchanged and fail the continuous-OFF
    // oracle. Its older entry lacks only the newly explicit snapshot proof.
    if constexpr(HasMotionSnapshotProof<F>)return prepare(list,context,slot,guides,feature,w,h,0,0,snapshot);
    else return prepare(list,context,slot,guides,feature,w,h,0,0);
}
nr_temporal::Prepared Prepare(ID3D12GraphicsCommandList* list,NrContext& context,NrSlot& slot,
    const NrGuideBinding& guides,const FeatureRecord& feature,unsigned w,unsigned h,bool snapshot=true){
    return PrepareCompatible(&nr_temporal::Prepare,list,context,slot,guides,feature,w,h,snapshot);
}
HRESULT STDMETHODCALLTYPE NativeReset(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator,ID3D12PipelineState* initial){
    return ID3D12GraphicsCommandList_Reset(list,allocator,initial);
}
// Exercise the same native Reset transaction as the hook, including its
// outer NR quarantine gate. Keep old allocators alive until GPU completion.
struct TrackedList {
    queue_test::Fixture& fixture;queue_test::List list;
    std::vector<ID3D12CommandAllocator*> allocators;
    explicit TrackedList(queue_test::Fixture& f):fixture(f),list(f){
        list.Close();list.Prime(f.a);list.Prime(f.a);f.Drain(f.a);Reset();
    }
    void Reset(){
        ID3D12CommandAllocator* allocator=nullptr;
        Hr(ID3D12Device_CreateCommandAllocator(fixture.device,D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_ID3D12CommandAllocator,(void**)&allocator),"tracked Reset allocator");
        allocators.push_back(allocator);
        Hr(ProcessCommandListReset(0,list.native,allocator,nullptr),"actual Core native Reset transaction");
    }
    void Submit(){list.Close();list.Submit(fixture.a);}
    ~TrackedList(){for(auto*& allocator:allocators)ReleaseCom(allocator);}
};
void Collect(){
    EnterCriticalSection(&g_chain_cs);CollectRetiredContextsLocked();LeaveCriticalSection(&g_chain_cs);
    DrainPendingContextDestructions();
}
void Configure(unsigned backend,unsigned scope,bool enabled=true){
    auto next=g_config;next.temporal_backend=backend;next.temporal_scope=scope;next.enabled=enabled;
    // Keep the actual two-layer configuration through ApplyConfig normalization.
    next.nr_layers[0].enabled=next.nr_layers[0].initialized=true;
    Check(ApplyConfig(next,"temporal-core-regression",false),"actual committed temporal configuration change");
    Check(g_config.temporal_backend==backend&&g_config.temporal_scope==scope&&g_config.enabled==enabled&&g_config.nr_passes==2,
        "committed temporal choice and layer count retained");
}
void RecordCurrent(queue_test::List& list,NrContext& context,NrSlot& slot,const NrGuideBinding& guides,
    const FeatureRecord& feature,const ColorContract& color,unsigned w,unsigned h,unsigned long long sequence){
    // The caller has already established the current NR lease, just as
    // PrepareAfterSr does before FinishAfterSr reaches temporal preparation.
    Check(context.recording_command_lists>0,"current NR lease exists before temporal preparation");
    nr_source_runtime::EvaluateScope source(list.native,nr_source_frame::Source{41,51,sequence},nr_source_runtime::NativeInputs{});
    auto prepared=Prepare(list.native,context,slot,guides,feature,w,h);
    if(!prepared)std::printf("continuous prepare declined=%s backend=%u scope=%u\n",
        nr_temporal_status::Name(nr_temporal_status::state.load()),g_config.temporal_backend,g_config.temporal_scope);
    Check(bool(prepared),"CORE_TEMPORAL_CONTINUOUS_PREPARE_ORACLE");
    Check(context.temporal_key==(g_config.temporal_backend|(g_config.temporal_scope<<8)),"requested runtime key actually created");
    if(prepared.primary_heap){
        RecordPostNrFields(list.native,context,slot,feature,color,w,h,prepared.primary_heap);
        DispatchCodec(list.native,slot,g_codec.combine,w,h,color,false,0,0,0,0,slot.codec_color_mode,w,h,w,h,1,1,prepared.primary_heap);
        UavBarrier(list.native,prepared.runtime->anchor());prepared.inputs.anchor_source=prepared.inputs.frame.source;
    }
    const auto before=nr_temporal_status::recorded.load();
    Check(nr_temporal::Record(list.native,context,slot,prepared),"continuous temporal recording accepted");
    Check(context.temporal_token&&nr_temporal_status::recorded.load()==before+1,"continuous frame records exactly once");
}
void Run(){
    queue_test::Fixture f;auto& context=g_contexts[0];auto& slot=context.slots[0];
    g_original_command_list_reset[0]=NativeReset;
    g_config=ChainConfig{};g_config.enabled=true;g_config.mode=1;g_config.transfer_strength=g_config.post_transfer_strength=1;
    g_config.processing_start=nr_processing::Start::After;g_config.processing_selection=1;
    g_config.color_strength=.7f;g_config.allow_unverified_hdr_color=true;g_config.preserve_lighting=false;g_config.lighting_lock=0;
    nr_processing_runtime::Reconcile();
    Check(nr_processing_runtime::Effective()==nr_processing::Start::After,"actual processing session selects After");
    Check(EnsureCodec(f.device),"complete production codec");
    constexpr unsigned w=128,h=128;context.width=w;context.height=h;context.instance_id=501;context.feature_generation=551;context.mode=1;
    context.sr_handle=reinterpret_cast<const NgxHandle*>(501); // Identity only; no NGX entry is invoked.
    ID3D12Resource* output=nullptr;Check(CreateTexture(f.device,w,h,L"temporal fixture output",&output),"output");
    Check(CreateSlot(f.device,output,w,h,true,nullptr,slot),"actual SDR post-SR slot");
    FeatureRecord feature{};feature.feature_id=1;NrGuideBinding guides{};ColorContract color{};
    ClearViews clear(f.device);
    {queue_test::List init(f);const float b[]={.25f,.375f,.5f,1},p[]={.4f,.4f,.4f,1},n[]={.62f,.53f,.56f,1};
        Transition(init.native,slot.result,slot.result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);clear.Clear(f.device,init.native,slot.result,0,b);
        PostSrSubstitution sub{};sub.slot=&slot;sub.width=w;sub.height=h;sub.work_width=w;sub.work_height=h;sub.native_output_snapshot=true;
        Check(DispatchPostSrNativeSnapshot(init.native,sub),"actual unenhanced snapshot");
        Transition(init.native,slot.proxy,slot.proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);clear.Clear(f.device,init.native,slot.proxy,1,p);
        Transition(init.native,slot.neural,slot.neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);clear.Clear(f.device,init.native,slot.neural,2,n);
        Transition(init.native,slot.proxy,slot.proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(init.native,slot.neural,slot.neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Submit(f,init);}
    const auto before=ReadTexture(f,output,slot.result_state);
    {queue_test::List off(f);Check(!nr_temporal::Prepare(off.native,context,slot,guides,feature,w,h,0,0),"OFF no preparation");
        Check(!context.temporal&&nr_temporal_status::bytes.load()==0&&nr_temporal_status::recorded.load()==0,"OFF no resources or recording");off.Close();}
    g_config.temporal_backend=1;
    Check(CreateTexture(f.device,w,h,L"prepared native motion",&slot.motion_proxy,DXGI_FORMAT_R16G16_FLOAT),"native guide texture");
    {queue_test::List init(f);const float zero[]={0,0,0,0};Transition(init.native,slot.motion_proxy,slot.motion_proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        clear.Clear(f.device,init.native,slot.motion_proxy,0,zero);Transition(init.native,slot.motion_proxy,slot.motion_proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Submit(f,init);}
    guides.signature.resample_motion=true;guides.signature.motion_scale_x=guides.signature.motion_scale_y=1;guides.capture_params.game_mv_valid=true;
    guides.has_motion_scale_x=guides.has_motion_scale_y=guides.valid_motion_scale_x=guides.valid_motion_scale_y=true;
    guides.capture_params.game_mv_x=guides.capture_params.game_mv_y=1;
    guides.motion=slot.motion_proxy;
    // Native single-layer composition is the independent P1 reference.
    {queue_test::List one(f);DispatchCodec(one.native,slot,g_codec.combine,w,h,color,false,0,0,0,0,slot.codec_color_mode,w,h,w,h,1,1);UavBarrier(one.native,slot.result);Submit(f,one);}
    const auto single=ReadTexture(f,output,slot.result_state);Check(single!=before,"nonzero single-layer composition reference");
    auto later_slot=std::make_unique<NrSlot>();
    g_config.nr_passes=2;g_config.temporal_scope=1;slot.dual_selected=later_slot.get();
    queue_test::List list(f);list.Close();list.Prime(f.a);list.Prime(f.a);f.Drain(f.a);list.ResetWithNewAllocator(f);
    Check(FindOrCreateLifecycleQueueLocked(f.a)!=nullptr,"actual queue and fence owner");
    Check(RecordNrUseLocked(list.native,context,slot),"actual Core recording lease");
    nr_source_runtime::EvaluateScope source(list.native,nr_source_frame::Source{41,51,1},nr_source_runtime::NativeInputs{});
    Check(bool(nr_source_runtime::CurrentSource()),"existing source scope supplies identity");
    feature.feature_id=13;Check(!nr_temporal::Prepare(list.native,context,slot,guides,feature,w,h,0,0),"RR outside current supported route");
    feature.feature_id=1;feature.create_flags=1;Check(!nr_temporal::Prepare(list.native,context,slot,guides,feature,w,h,0,0),"HDR rejected");feature.create_flags=0;
    g_config.face_preview=1;Check(!nr_temporal::Prepare(list.native,context,slot,guides,feature,w,h,0,0),"face preview rejected");g_config.face_preview=0;
    auto prepared=Prepare(list.native,context,slot,guides,feature,w,h);
    if(!prepared)std::printf("prepare declined=%s\n",nr_temporal_status::Name(nr_temporal_status::state.load()));
    Check(bool(prepared)&&prepared.primary_heap,"actual native temporal and primary descriptor preparation");
    RecordPostNrFields(list.native,context,slot,feature,color,w,h,prepared.primary_heap);
    const auto captures=slot.recovery_probe_count;
    DispatchCodec(list.native,slot,g_codec.combine,w,h,color,false,0,0,0,0,slot.codec_color_mode,w,h,w,h,1,1,prepared.primary_heap);
    UavBarrier(list.native,prepared.runtime->anchor());prepared.inputs.anchor_source=prepared.inputs.frame.source;
    Check(slot.recovery_probe_count==captures,"primary anchor does not claim final diagnostics");
    Check(nr_temporal::Record(list.native,context,slot,prepared),"actual Core records final residual");
    Check(context.temporal_token&&nr_temporal_status::recorded.load()==1&&nr_temporal_status::submitted.load()==0,"recording is not adoption");
    list.Close();list.Submit(f.a);f.Drain(f.a);
    Check(!g_lifecycle_tracking_uncertain.load()&&nr_temporal_status::submitted.load()==1&&!context.temporal_token,"actual Core Execute Signal adopts temporal once");
    auto anchor_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;const auto anchor=ReadTexture(f,context.temporal->anchor(),anchor_state);
    Check(anchor==single,"P1_IS_EXISTING_SINGLE_LAYER_COMPOSITION_ORACLE");
    const auto final=ReadTexture(f,output,slot.result_state);Check(final==single,"zero later edit preserves exact actual current candidate");
    // A completed fence does not end the old command list's replay lease.
    // Keep a NEW NR-only recording alive while the normal collector handles OFF.
    TrackedList ongoing(f);
    Check(RecordNrUseLocked(ongoing.list.native,context,slot),"continuous NR starts before old temporal retirement");
    auto* old=context.temporal;const auto old_bytes=nr_temporal_status::bytes.load();
    Configure(0,1);Collect();
    Check(context.temporal==old&&old_bytes&&nr_temporal_status::bytes.load()==old_bytes,
        "CORE_TEMPORAL_COMPLETED_FENCE_STILL_REPLAYABLE_ORACLE");
    ID3D12CommandAllocator* allocator=nullptr;
    Hr(ID3D12Device_CreateCommandAllocator(f.device,D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)&allocator),"completed reset allocator");
    Hr(ProcessCommandListReset(0,list.native,allocator,nullptr),"actual completed Core Reset");list.Close();Collect();
    Check(context.recording_command_lists>0&&!context.retired,"NR parent remains live with a current recording");
    Check(!context.temporal&&nr_temporal_status::bytes.load()==0,"CORE_TEMPORAL_OFF_DURING_CONTINUOUS_NR_ORACLE");
    const auto off_submissions=nr_temporal_status::submitted.load();
    Check(!nr_temporal::Prepare(ongoing.list.native,context,slot,guides,feature,w,h,0,0),"OFF current NR does not prepare temporal");
    ongoing.Submit();f.Drain(f.a);ongoing.Reset();
    Check(nr_temporal_status::submitted.load()==off_submissions&&ReadTexture(f,output,slot.result_state)==single,
        "OFF continues NR without temporal adoption or candidate damage");
    // The same-size native field is valid even when NR itself needs no MV
    // resample. Its owned snapshot must have explicit current-recording proof.
    if constexpr(HasMotionSnapshotProof<decltype(&nr_temporal::Prepare)>)guides.signature.resample_motion=false;

    // Each change occurs before the previous temporal list is Reset. The
    // three-list ring keeps a current NR owner during every safe replacement.
    TrackedList ring0(f),ring1(f),ring2(f);TrackedList* ring[]={&ring0,&ring1,&ring2};
    constexpr unsigned choices[][2]={{1,0},{1,1},{2,1},{2,0},{1,0}};
    TrackedList* previous=nullptr;unsigned long long sequence=2;
    for(unsigned i=0;i<std::size(choices);++i){
        Configure(choices[i][0],choices[i][1]);
        auto& current=*ring[i%std::size(ring)];
        Check(RecordNrUseLocked(current.list.native,context,slot),"new NR ring recording owns parent before switching");
        if(previous){
            auto* retained=context.temporal;Collect();
            Check(retained&&context.temporal==retained,"requested switch retains completed but replayable temporal");
            previous->Reset();Collect();
            Check(context.recording_command_lists>0&&!context.temporal,"CORE_TEMPORAL_SCOPE_BACKEND_RETIRE_ORACLE");
        }
        if constexpr(HasMotionSnapshotProof<decltype(&nr_temporal::Prepare)>){
            if(choices[i][0]==1){
                nr_source_runtime::EvaluateScope proof_source(current.list.native,nr_source_frame::Source{41,51,sequence},nr_source_runtime::NativeInputs{});
                Check(!Prepare(current.list.native,context,slot,guides,feature,w,h,false),"same-size native MV without snapshot proof rejected");
            }
        }
        RecordCurrent(current.list,context,slot,guides,feature,color,w,h,sequence++);
        const auto adopted=nr_temporal_status::submitted.load();current.Submit();f.Drain(f.a);
        Check(nr_temporal_status::submitted.load()==adopted+1&&!context.temporal_token&&!g_lifecycle_tracking_uncertain.load(),
            "chosen backend and scope reach actual Execute and Signal");
        Check(nr_temporal_status::bytes.load()>0&&nr::final_residual::RecordedTemporal::collect_retired().active==1,
            "continuous switches retain one bounded temporal instance");
        previous=&current;
    }

    // Replay is a new GPU execution, not a new source frame. Hold its final
    // fence, then Reset; neither the earlier fence nor Reset alone may release.
    auto* replayed=context.temporal;const auto earlier_fence=context.completion_fence_value;
    const auto before_replay=nr_temporal_status::submitted.load();ID3D12Fence* gate=f.Fence();
    Hr(ID3D12CommandQueue_Wait(f.a,gate,1),"hold replay's real last consumer");
    previous->list.Submit(f.a);
    Check(context.completion_fence_value>earlier_fence&&!IsContextFenceCompleteLocked(context),"replay replaces earlier completed fence");
    Check(nr_temporal_status::submitted.load()==before_replay&&!context.temporal->initialized(),"replay retires history without inventing a source frame");
    auto& restart=ring2;
    Check(RecordNrUseLocked(restart.list.native,context,slot),"NR continues while old replay completion is pending");
    previous->Reset();Collect();
    Check(context.temporal==replayed&&!IsContextFenceCompleteLocked(context),"CORE_TEMPORAL_RESET_WITH_PENDING_FENCE_ORACLE");
    Hr(ID3D12Fence_Signal(gate,1),"release actual replay fence");f.Drain(f.a);Collect();
    Check(context.recording_command_lists>0&&!context.temporal,"CORE_TEMPORAL_REPLAY_SAFE_RECREATE_ORACLE");
    RecordCurrent(restart.list,context,slot,guides,feature,color,w,h,sequence++);
    restart.Submit();f.Drain(f.a);restart.Reset();
    Check(context.temporal&&context.temporal->initialized(),"ordinary completed Reset retains reusable same-key history");
    Configure(1,0,false);Collect();
    Check(!context.temporal&&nr_temporal_status::bytes.load()==0,"CORE_TEMPORAL_MASTER_OFF_MAINTENANCE_ORACLE");
    Check(ReadTexture(f,output,slot.result_state)==single,"all temporal transitions preserve the actual zero-edit candidate");
    ReleaseCom(gate);ReleaseCom(allocator);

    // The outer NR Reset gate is authoritative: temporal's local cancellation
    // API does not prove opaque Feature18 state is safe after discarded NR.
    // End with this isolated quarantine case; no resources are force-released.
    Configure(1,0,true);TrackedList abandoned(f);
    Check(RecordNrUseLocked(abandoned.list.native,context,slot),"unsubmitted full Core recording owner");
    RecordCurrent(abandoned.list,context,slot,guides,feature,color,w,h,sequence++);
    const auto cancelled=nr_temporal_status::cancelled.load();const auto submitted=nr_temporal_status::submitted.load();
    auto* quarantined=context.temporal;abandoned.list.Close();abandoned.Reset();Collect();
    Check(context.temporal==quarantined&&context.temporal_token&&context.lifecycle_dependencies_quarantined&&g_lifecycle_tracking_uncertain.load(),
        "CORE_UNSUBMITTED_NR_RESET_QUARANTINE_ORACLE");
    Check(nr_temporal_status::cancelled.load()==cancelled&&nr_temporal_status::submitted.load()==submitted,
        "outer NR quarantine is neither temporal cancellation nor adoption");
    Configure(0,0);Collect();
    Check(context.temporal==quarantined&&nr_temporal_status::bytes.load()>0,"OFF preserves quarantined dependency closure");
    ReleaseCom(output);
    std::printf("PASS temporal Core P1 continuous OFF/native/AMD/scope switches replay fences and real Reset quarantine checks=%u; synthetic NR, WARP only\n",queue_test::checks);
}
}
