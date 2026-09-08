void TestGpuPipeline() {
    Reset();config.enabled=true;config.mode=2;config.transfer_strength=config.post_transfer_strength=1;
    config.color_strength=0;reshade::api::effect_runtime runtime;Start(runtime);test_clock+=3100;
    constexpr unsigned side=512;
    Gpu gpu;nr_f8_gpu::CaptureRing ring;
    // Recreate a CPU-only descriptor source. Production Arm explicitly rejects
    // copying from a shader-visible heap; the detached destination is GPU visible.
    gpu.heap=Com<ID3D12DescriptorHeap>();
    D3D12_DESCRIPTOR_HEAP_DESC hd={};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=kCodecDescriptorCount;
    Check(gpu.device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,gpu.heap.address()),"CPU descriptor mirror");
    const auto fence=[](void *key,unsigned long long value){auto *f=static_cast<ID3D12Fence *>(key);return f->GetCompletedValue()!=UINT64_MAX && f->GetCompletedValue()>=value;};
    const auto deliver=[](void *,nr_f8_gpu::ReadbackJob *job){nr_f8::RawReady(job->ticket,job->info,job->readback,job->bytes,job->valid);job->readback=nullptr;delete job;};
    for(unsigned i=0;i<kCurrentRoiFrames+1;++i) {
        auto info=Info(i==kCurrentRoiFrames?900:100+i,side,side);info.codec_flags=528;info.transfer_strength=1;info.color_strength=0;
        info.source_format=info.result_format=DXGI_FORMAT_R32G32B32A32_FLOAT;info.proxy_scale=1;info.pre_exposure=1;info.exposure_scale=1;
        info.has_pre_exposure=info.has_exposure_scale=true;info.frame_confidence=1;info.max_luma_ratio=2;
        info.identity.tuning.intensity=1;info.identity.tuning.local_tone=info.identity.tuning.local_structure=1;
        NrCaptureParams params={};params.reset_known=true;params.game_jitter_valid=true;params.game_jitter_x=params.game_jitter_y=0;
        FrameTicket ticket;Check(ReserveFrame(info,params,ticket),"WARP frame reserves immutable identity");
        auto source=gpu.MakeTexture(side,side,std::vector<Float4>(side*side,Float4{.2f,.3f,.4f,.7f}));
        auto proxy=gpu.MakeTexture(side,side,std::vector<Float4>(side*side,Float4{.4f,.5f,.6f,1}));
        auto neural=gpu.MakeTexture(side,side,std::vector<Float4>(side*side,Float4{.5f,.6f,.7f,1}));
        auto output=gpu.MakeTexture(side,side,std::vector<Float4>(side*side,Float4{}));
        gpu.TextureState(source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);gpu.TextureState(proxy,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gpu.TextureState(neural,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);gpu.TextureState(output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        gpu.Srv(0,source.resource.p);gpu.Srv(1,proxy.resource.p);gpu.Srv(2,neural.resource.p);gpu.Srv(3,nullptr);gpu.Srv(kCodecExposureSrvIndex,nullptr);
        gpu.UavTexture(kCodecSrvCount,nullptr,DXGI_FORMAT_R32G32B32A32_FLOAT);gpu.UavTexture(kCodecResultUavIndex,output.resource.p,DXGI_FORMAT_R32G32B32A32_FLOAT);
        gpu.UavTexture(kCodecMotionUavIndex,nullptr,DXGI_FORMAT_R32G32_FLOAT);gpu.UavBuffer(kCodecStatsUavIndex,nullptr,0);gpu.UavBuffer(kCodecCaptureUavIndex,nullptr,0);
        nr_f8_gpu::ArmArgs args={};args.device=gpu.device.p;args.source_heap=gpu.heap.p;args.descriptor_count=kCodecDescriptorCount;
        args.descriptor_size=gpu.increment;args.capture_uav_index=kCodecCaptureUavIndex;args.request=ticket.session;args.group=ticket.group;
        args.frame_index=ticket.index;args.identity=info.identity;args.roi_width=info.region_width;args.roi_height=info.region_height;args.info=&info;
        if(i==0){
            nr_f8_gpu::ArmResult unused;auto invalid=args;invalid.info=nullptr;Check(!ring.Arm(invalid,unused),"missing five-plane metadata rejected");
            Check(ring.Arm(args,unused),"unreferenced arm created");Check(!ring.BeginDispatch(gpu.list.p,unused.slot,0),"zero recording token rejected before first reference");
            Check(ring.DiscardUnrecorded(unused.slot),"unreferenced allocation may be discarded");
            auto interrupted=args;interrupted.request=999999;Check(ring.Arm(interrupted,unused),"interrupted recording arm");
            Check(ring.BeginDispatch(gpu.list.p,unused.slot,8000),"first GPU reference establishes token ownership");
            Check(!ring.FinishDispatch(gpu.list.p,unused.slot,8001),"wrong recording token cannot finish readback");ring.Destroy();
            Check(ring.ledger().Get(unused.slot).state==nr_f8_gpu::TokenState::Recorded,"Destroy retains allocation referenced before unfinished copy");
            Check(ring.OnSubmit(8000,gpu.fence.p,gpu.submitted+1)==1,"interrupted recording exact fence registered");
        }
        nr_f8_gpu::ArmResult arm;Check(ring.Arm(args,arm),"production detached GPU Arm");Check(ring.BeginDispatch(gpu.list.p,arm.slot,1000+i),"production begin transition");
        const CodecConstants constants={side,side,1,1,1,0,2,528,0,0,0,0,0,0,0,0,1,info.stride,side,side,side,side,0,0,1,1,0,info.region_x,info.region_y,info.region_width,info.region_height};
        ID3D12DescriptorHeap *heaps[]={arm.heap};gpu.list->SetDescriptorHeaps(1,heaps);gpu.list->SetComputeRootSignature(gpu.root.p);
        gpu.list->SetComputeRootDescriptorTable(0,arm.heap->GetGPUDescriptorHandleForHeapStart());gpu.list->SetComputeRoot32BitConstants(1,kCodecRootConstantCount,&constants,0);
        gpu.list->SetPipelineState(gpu.v2_pipeline.p);gpu.list->Dispatch(side/8,side/8,1);
        Check(ring.FinishDispatch(gpu.list.p,arm.slot,1000+i),"production five-plane copy recording");
        Check(ring.OnSubmit(1000+i,gpu.fence.p,gpu.submitted+1)==1,"exact fence submission registered");
        Check(ring.Collect(fence,deliver,nullptr)==0,"no readback before actual queue fence");
        gpu.Execute(); // Actual ExecuteCommandLists, Signal, wait and Reset.
        Check(ring.Collect(fence,deliver,nullptr)==0,"fence alone cannot retire a live recording token");
        Check(ring.OnRecordingInvalidated(1000+i,false)==1,"successful real Reset invalidates token");
        Check(ring.Collect(fence,deliver,nullptr)==1,"fence plus token dispatches production readback worker");Drain();
        if(i==0){Check(ring.OnRecordingInvalidated(8000,false)==1,"interrupted token invalidated after real Reset");Check(ring.Collect(fence,deliver,nullptr)==1,"interrupted copy retires only with exact fence plus dead token");Drain();}
        Check(g_session->groups[0].frames[ticket.index].valid,"real GPU record streamed");
    }
    Present(&runtime);Shot(runtime);Present(&runtime);Drain();
    Check(g_status==Status::Complete,"actual GPU pipeline wrote complete ZIP");
    Check(!nr_f8_budget::gpu_bytes && !nr_f8_budget::cpu_bytes,"actual GPU/CPU budget drained");
    std::wstring dest=output_root+L"\\feedback.zip";Check(CopyFileW(g_last_zip,dest.c_str(),TRUE)!=FALSE,"publish WARP parser fixture");
    char utf8[4096];WideToUtf8(dest.c_str(),utf8,sizeof(utf8));std::printf("WARP-ZIP: %s\n",utf8);
}
