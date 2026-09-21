// Actual Core consumers, immutable binding packets and Execute/Signal/Reset.
// Constructed NR1/NR2 surfaces: this is not NGX inference or a game test.
namespace temporal_core_test {
using queue_test::Check;using queue_test::Hr;
using post_output_test::Submit;using post_output_test::ReadTexture;using post_output_test::ClearViews;
void Run(){
    queue_test::Fixture f;auto& context=queue_test::context;auto& slot=context.slots[0];
    g_config=ChainConfig{};g_config.enabled=true;g_config.mode=1;g_config.transfer_strength=g_config.post_transfer_strength=1;
    g_config.color_strength=.7f;g_config.allow_unverified_hdr_color=true;g_config.preserve_lighting=false;g_config.lighting_lock=0;
    nr_processing_runtime::Reconcile();
    Check(EnsureCodec(f.device),"complete production codec");
    constexpr unsigned w=128,h=128;context.width=w;context.height=h;context.instance_id=501;context.feature_generation=551;context.mode=1;
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
    // Native single-layer composition is the independent P1 reference.
    {queue_test::List one(f);DispatchCodec(one.native,slot,g_codec.combine,w,h,color,false,0,0,0,0,slot.codec_color_mode,w,h,w,h,1,1);UavBarrier(one.native,slot.result);Submit(f,one);}
    const auto single=ReadTexture(f,output,slot.result_state);Check(single!=before,"nonzero single-layer composition reference");
    g_config.nr_passes=2;g_config.temporal_scope=1;slot.dual_selected=true;
    queue_test::List list(f);list.Close();list.Prime(f.a);list.Prime(f.a);f.Drain(f.a);list.ResetWithNewAllocator(f);
    Check(FindOrCreateLifecycleQueueLocked(f.a)!=nullptr,"actual queue and fence owner");
    Check(RecordNrUseLocked(list.native,context,slot),"actual Core recording lease");
    nr_source_runtime::EvaluateScope source(list.native,nr_source_frame::Source{41,51,1},nr_source_runtime::NativeInputs{});
    Check(bool(nr_source_runtime::CurrentSource()),"existing source scope supplies identity");
    feature.feature_id=13;Check(!nr_temporal::Prepare(list.native,context,slot,guides,feature,w,h,0,0),"RR outside current supported route");
    feature.feature_id=1;feature.create_flags=1;Check(!nr_temporal::Prepare(list.native,context,slot,guides,feature,w,h,0,0),"HDR rejected");feature.create_flags=0;
    g_config.face_preview=1;Check(!nr_temporal::Prepare(list.native,context,slot,guides,feature,w,h,0,0),"face preview rejected");g_config.face_preview=0;
    auto prepared=nr_temporal::Prepare(list.native,context,slot,guides,feature,w,h,0,0);
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
    // End the real recording first, then the Core lifetime callback releases
    // its descriptor/resource leases. No token is treated as GPU completion.
    ID3D12CommandAllocator* allocator=nullptr;Hr(ID3D12Device_CreateCommandAllocator(f.device,D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)&allocator),"completed reset allocator");
    Hr(ID3D12GraphicsCommandList_Reset(list.native,allocator,nullptr),"actual completed Reset");list.Detach();list.Close();
    g_config.temporal_backend=0;Check(nr_temporal::ReleaseIdle(context),"OFF releases only safe idle temporal context");
    Check(!context.temporal&&nr_temporal_status::bytes.load()==0,"temporal resource account returns after safe retirement");
    DestroyBindings(context);DestroySlot(slot);ReleaseCom(output);ReleaseCom(allocator);
    std::printf("PASS temporal Core preparation P1 submit reset checks=%u; synthetic NR, WARP only\n",queue_test::checks);
}
}
