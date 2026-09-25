// Complete production Core. P/N are constructed planes, not NR inference.
// Arm/Finish/Collect/export use production code, real command observers,
// real compositor CSO, typed WARP storage and exact Execute/Signal retirement.
namespace native_capture_test {
using queue_test::Check;using queue_test::Hr;
struct Params {
    NgxParams base{};void* table[17]{};ID3D12Resource* output=nullptr;
    unsigned x=3,y=2,w=11,h=9;
    static NgxResult Resource(const NgxParams* p,const char* k,ID3D12Resource** r){auto& s=*reinterpret_cast<const Params*>(p);
        *r=!std::strcmp(k,"Output")?s.output:nullptr;return *r?1:0;}
    static NgxResult UInt(const NgxParams* p,const char* k,unsigned* v){auto& s=*reinterpret_cast<const Params*>(p);
        if(!std::strcmp(k,"DLSS.Output.Subrect.Base.X"))*v=s.x;
        else if(!std::strcmp(k,"DLSS.Output.Subrect.Base.Y"))*v=s.y;
        else if(!std::strcmp(k,"DLSS.Output.Subrect.Dimensions.Width"))*v=s.w;
        else if(!std::strcmp(k,"DLSS.Output.Subrect.Dimensions.Height"))*v=s.h;
        else return 0;return 1;}
    Params(ID3D12Resource* r):output(r){base.vtable=table;table[9]=(void*)Resource;table[12]=(void*)UInt;}
};
void Run(const wchar_t* directory,unsigned scenario) {
    using namespace nr_native_capture;
    queue_test::Fixture f;
    swprintf_s(g_log_path,L"%ls\\capture-test.log",directory);
    g_config=ChainConfig{};g_config.enabled=true;g_config.mode=2;g_config.processing_start=nr_processing::Start::Before;
    g_config.processing_selection=1;g_config.transfer_strength=1;g_config.color_strength=.7f;
    nr_processing_runtime::Reconcile();descriptor_warp_test::InstallCommandObservation(f);
    Check(EnsureCodec(f.device),"actual production compositor CSO");
    ID3D12Resource *original=nullptr,*output=nullptr;
    Check(CreateTexture(f.device,8,6,L"constructed NR original",&original),"original texture");
    Check(CreateTexture(f.device,17,13,L"native O distinct target",&output),"output texture");
    auto& context=g_contexts[0];auto& slot=context.slots[0];
    context.sr_handle=reinterpret_cast<const NgxHandle*>(501);context.feature_generation=551;
    context.instance_id=501;context.width=8;context.height=6;context.mode=2;
    Check(CreateSlot(f.device,original,8,6,false,nullptr,slot,CodecColorMode::LinearSdr),"actual pre-SR slot");
    Check(RequestStageCapture(),"ordinary capture request");const auto request=g_stage_capture_request_id.load();
    // Explicit constructed-stage fixture. This is not 64 actual NR evaluations.
    g_stage_capture_rebuild_pending=false;context.stage_capture_request_id=request;
    ArmStageCaptureLocked(context,slot);Check(slot.stage_capture.armed,"production stage allocation");
    slot.stage_capture.info.identity={};auto& id=slot.stage_capture.info.identity;
    id.instance_id=501;id.frame_id=1;id.config_generation=1;id.evaluation=id.since_reset=64;id.warmed_up=true;
    post_output_test::ClearViews clear(f.device);
    {queue_test::List init(f);const float b[]={.25f,.375f,.5f,1},p[]={.4f,.4f,.4f,1},n[]={.62f,.53f,.56f,1};
        auto state=D3D12_RESOURCE_STATE_COMMON;Transition(init.native,original,state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);clear.Clear(f.device,init.native,original,0,b);
        Transition(init.native,original,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(init.native,slot.proxy,slot.proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);clear.Clear(f.device,init.native,slot.proxy,1,p);
        Transition(init.native,slot.neural,slot.neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);clear.Clear(f.device,init.native,slot.neural,2,n);
        Transition(init.native,slot.proxy,slot.proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(init.native,slot.neural,slot.neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);post_output_test::Submit(f,init);}
    queue_test::List list(f);Check(RecordNrUseLocked(list.native,context,slot),"actual recording lease");
    const auto coverage=EnsurePostBoundaryNativeHooks(list.native);Check(!(coverage&nr_post_boundary::HookMissing),"actual barrier hook coverage");
    Params params(output);FeatureRecord feature{};feature.feature_id=1;feature.generation=551;feature.out_width=11;feature.out_height=9;
    nr_source_runtime::StreamlineScope source({41,51,1});
    nr_source_runtime::EvaluateScope evaluation(list.native,context.sr_handle,&params.base,feature);
    Check(bool(nr_source_runtime::CurrentSource()),"constructed same-call source token");
    ColorContract color{};Transition(list.native,slot.result,slot.result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchCodec(list.native,slot,g_codec.combine_v2,8,6,color,false,0,0,0,0,CodecColorMode::LinearSdr,8,6,8,6);
    Check(slot.stage_capture.pending,"P/N/E actually written by compositor");
    auto* capture=ArmNativeOutputCapture(list.native,&params.base,feature,&slot);
    if(!capture||capture->header.status!=Status::Armed)std::printf("arm reason=%s\n",capture?ReasonName(capture->header.reason):"null");
    Check(capture&&capture->header.status==Status::Armed,"same-call O armed");
    const float native[]={.125f,.25f,.75f,1};auto state=D3D12_RESOURCE_STATE_COMMON;
    Transition(list.native,output,state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);clear.Clear(f.device,list.native,output,0,native);
    if(scenario==1){D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={output,0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        ID3D12GraphicsCommandList_ResourceBarrier(list.native,1,&barrier);} // Partial transition invalidates all-resource state evidence.
    if(scenario==2)++evaluation.source.sequence;
    if(scenario==3)++capture->header.reset; // Refusal input only, never changes real command recording.
    FinishNativeOutputCapture(list.native,&params.base,scenario==4?0xBAD00001u:kNgxSuccess,capture);
    const Reason expected[]={Reason::None,Reason::StateUnknown,Reason::SourceChanged,Reason::RecordingChanged,Reason::NativeFailed};
    Check(capture->header.reason==expected[scenario],"exact refusal reason");
    Check(capture->header.status==(scenario?Status::Missing:Status::Recorded),"no premature completion declaration");
    CollectStageCaptureLocked(slot);Check(slot.stage_capture.pending&&StageCaptureStatus()==2,"no readback/export before Execute");
    list.Close();list.Submit(f.a);queue_test::Wait(slot.last_submission_queue->fence,slot.completion_fence_value);
    CollectStageCaptureLocked(slot);Check(slot.stage_capture.pending,"completed GPU keeps live recording lease");
    list.Detach();CollectStageCaptureLocked(slot);
    const auto deadline=GetTickCount64()+10000;while(StageCaptureStatus()!=0&&GetTickCount64()<deadline)Sleep(1);
    Check(StageCaptureStatus()==0,"bounded exporter completion");StageCaptureExportReceipt receipt{};
    Check(ReadStageCaptureExportReceipt(request,receipt)&&receipt.stages_complete,"complete parent receipt");
    Check(receipt.native_status==(scenario?Status::Missing:Status::Complete)&&receipt.native_reason==expected[scenario],"O receipt preserves completion/refusal");
    if(!scenario){wchar_t path[MAX_PATH]{};swprintf_s(path,L"%ls\\native-output.nro",receipt.directory);FILE* file=_wfopen(path,L"rb");Check(file!=nullptr,"O pixels exported");
        Header h{};Check(fread(&h,1,sizeof(h),file)==sizeof(h)&&ValidPixels(h),"actual O header validates");
        std::vector<unsigned short> pixels(h.payload_bytes/2);Check(fread(pixels.data(),1,h.payload_bytes,file)==h.payload_bytes,"tight active-region payload");fclose(file);
        const unsigned short reference[]={0x3000,0x3400,0x3a00,0x3c00};
        for(unsigned i=0;i<pixels.size();++i)Check(pixels[i]==reference[i%4],"O has distinct native output pixels, not E or another frame");
    }
    std::printf("PASS native O capture scenario=%u checks=%u; constructed P/N and native clear, real WARP compositor/storage/fences; directory=%ls\n",scenario,queue_test::checks,receipt.directory);
    // Host exit owns process-pinned hooks; all pending GPU work already retired.
}
}
