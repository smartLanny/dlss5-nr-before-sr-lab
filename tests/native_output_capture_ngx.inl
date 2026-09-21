// Artificial static scene and guides; real signed Feature18 -> Feature1 through
// the complete production DispatchEvaluate/PrepareBeforeSr, not private SR.
namespace native_capture_ngx_test {
using queue_test::Check;using queue_test::Hr;
void Ngx(NgxResult value,const char* name){if(value!=kNgxSuccess)std::printf("NGX %s=%08x\n",name,value);Check(value==kNgxSuccess,name);}
ID3D12Resource* Plane(queue_test::Fixture& f,unsigned w,unsigned h,DXGI_FORMAT format,bool output){
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
    d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=format;
    if(output)d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ID3D12Resource* texture=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&texture),"artificial texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total=0;ID3D12Device_GetCopyableFootprints(f.device,&d,0,1,0,&fp,nullptr,nullptr,&total);
    d={};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=total;d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type=D3D12_HEAP_TYPE_UPLOAD;ID3D12Resource* upload=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_ID3D12Resource,(void**)&upload),"upload");
    unsigned char* pixels=nullptr;Hr(ID3D12Resource_Map(upload,0,nullptr,(void**)&pixels),"map scene");std::memset(pixels,0,(size_t)total);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        auto* p=pixels+y*fp.Footprint.RowPitch;
        if(format==DXGI_FORMAT_R16G16B16A16_FLOAT){const unsigned short rgba[]={static_cast<unsigned short>(0x3400+(x*256/w)),0x3800,0x3600,0x3c00};std::memcpy(p+x*8,rgba,8);}
        if(format==DXGI_FORMAT_R32_FLOAT){const float depth=.5f;std::memcpy(p+x*4,&depth,4);}
    }
    ID3D12Resource_Unmap(upload,0,nullptr);
    queue_test::List list(f);D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=texture;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    b.pResource=upload;b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(list.native,&a,0,0,0,&b,nullptr);
    auto state=D3D12_RESOURCE_STATE_COPY_DEST;Transition(list.native,texture,state,output?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    post_output_test::Submit(f,list);ReleaseCom(upload);return texture;
}
void Run(const wchar_t* directory){
    queue_test::Fixture f;swprintf_s(g_log_path,L"%ls\\native-ngx.log",directory);
    swprintf_s(g_ini_path,L"%ls\\host.ini",directory);
    g_config=ChainConfig{};g_config.enabled=true;g_config.mode=2;g_config.processing_start=nr_processing::Start::Before;g_config.processing_selection=1;
    g_config.warmup_after_create=2;g_config.transfer_strength=1;g_config_generation=1;g_requested_mode_fast=2;
    nr_processing_runtime::Reconcile();descriptor_warp_test::InstallCommandObservation(f);
    Check(EnsureNrApi(f.device),"actual signed NR bootstrap");Check(EnsureCodec(f.device),"actual production CSOs");
    HMODULE core=GetModuleHandleW(L"_nvngx.dll");Check(core!=nullptr,"official NGX core");
    auto create=(CreateFeatureFn)GetProcAddress(core,"NVSDK_NGX_D3D12_CreateFeature");
    auto evaluate=(EvaluateFeatureFn)GetProcAddress(core,"NVSDK_NGX_D3D12_EvaluateFeature");
    auto release=(ReleaseFeatureFn)GetProcAddress(core,"NVSDK_NGX_D3D12_ReleaseFeature");
    Check(create&&evaluate&&release,"original actual Feature1 exports");
    NgxParams* params=nullptr;Ngx(g_nr_api.allocate(&params),"allocate Feature1 parameters");
    for(auto pair:{std::pair{"Width",320u},{"Height",192u},{"OutWidth",640u},{"OutHeight",384u},{"CreationNodeMask",1u},{"VisibilityNodeMask",1u}})SetUInt(params,pair.first,pair.second);
    SetInt(params,"PerfQualityValue",0);SetInt(params,"DLSS.Feature.Create.Flags",2); // SDR, low-resolution motion.
    NgxHandle* handle=nullptr;{queue_test::List list(f);Ngx(create(list.native,1,params,&handle),"actual Feature1 Create");post_output_test::Submit(f,list);}
    Check(handle!=nullptr,"actual SR handle");RememberFeature(handle,1,params);
    auto* color=Plane(f,320,192,DXGI_FORMAT_R16G16B16A16_FLOAT,false);
    auto* depth=Plane(f,320,192,DXGI_FORMAT_R32_FLOAT,false);
    auto* motion=Plane(f,320,192,DXGI_FORMAT_R16G16_FLOAT,false);
    auto* output=Plane(f,640,384,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    SetResource(params,"Color",color);SetResource(params,"Depth",depth);SetResource(params,"MotionVectors",motion);SetResource(params,"Output",output);
    SetUInt(params,"DLSS.Render.Subrect.Dimensions.Width",320);SetUInt(params,"DLSS.Render.Subrect.Dimensions.Height",192);
    SetFloat(params,"DLSS.MV.Scale.X",1);SetFloat(params,"DLSS.MV.Scale.Y",1);
    SetFloat(params,"DLSS.Jitter.Offset.X",0);SetFloat(params,"DLSS.Jitter.Offset.Y",0);
    SetFloat(params,"DLSS.Pre.Exposure",1);SetFloat(params,"DLSS.Exposure.Scale",1);SetFloat(params,"DLSS.FrameTimeDeltaInMsec",16.6667f);
    Check(RequestStageCapture(),"real pre-SR deep request");const auto request=g_stage_capture_request_id.load();
    bool captured=false;
    for(unsigned frame=1;frame<=84&&!captured;++frame){
        queue_test::List list(f);EnsurePostBoundaryNativeHooks(list.native);
        // The host owns the prior state; publish real legacy transitions in this
        // exact recording. This is an artificial legal-state fixture, not a
        // claim that every game's SR supplies such evidence.
        auto state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;Transition(list.native,output,state,D3D12_RESOURCE_STATE_COPY_DEST);Transition(list.native,output,state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        SetInt(params,"Reset",frame==1?1:0);SetInt(params,"DLSS.Reset",frame==1?1:0);
        const auto result=DispatchEvaluate(evaluate,list.native,handle,params,nullptr);Ngx(result,"real production NR then native SR");
        list.Close();list.Submit(f.a);f.Drain(f.a);list.Detach();
        EnterCriticalSection(&g_chain_cs);for(auto& context:g_contexts)for(auto& slot:context.slots)CollectStageCaptureLocked(slot);LeaveCriticalSection(&g_chain_cs);
        StageCaptureExportReceipt receipt{};captured=ReadStageCaptureExportReceipt(request,receipt);
        std::printf("frame=%u requestStatus=%u nrFrame=%llu bypass=%d captured=%d\n",frame,StageCaptureStatus(),g_next_nr_frame_id,g_last_bypass_reason.load(),captured);
        if(StageCaptureStatus()==3){const auto end=GetTickCount64()+10000;while(StageCaptureStatus()!=0&&GetTickCount64()<end)Sleep(1);captured=ReadStageCaptureExportReceipt(request,receipt);}
    }
    StageCaptureExportReceipt receipt{};Check(captured&&ReadStageCaptureExportReceipt(request,receipt)&&receipt.stages_complete,"actual Feature18 stages exported");
    std::printf("actual native O status=%u reason=%s directory=%ls\n",unsigned(receipt.native_status),nr_native_capture::ReasonName(receipt.native_reason),receipt.directory);
    Check(receipt.native_status==nr_native_capture::Status::Complete,"actual same-call Feature1 O exported");
    std::printf("PASS actual Feature18 -> native Feature1 P/N/E/O; checks=%u; static artificial scene, zero-MV planar guides, no game or convergence claim\n",queue_test::checks);
}
}
