// Fixed artificial scene/guides, real signed Feature18 -> Feature1 through
// DispatchEvaluate. Pass-through observations never replace model results.
namespace model_boundary_ngx {
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
FILE* events=nullptr;
NrApi::BridgeCreateFn original_create=nullptr;
NrApi::BridgeEvaluateFn original_evaluate=nullptr;
NrApi::BridgeReleaseFn original_release=nullptr;
unsigned creates=0,releases=0,evaluations=0,segment=0;
ID3D12Resource* last_output=nullptr;
int last_reset=-1;
float wanted=0;
void Record(const char* event,const NgxParams* p,const void* handle,NgxResult result){
    float intensity=0,tone=0,structure=0,skin=0,exposure=0,mvx=0,mvy=0;
    int reset=-1,mask=-1,flags=0;unsigned style=0,ui=0,width=0,height=0;
    if(p){
        Check(GetFloat(p,"DLSSNR.Intensity",&intensity),"typed float intensity");
        Check(GetFloat(p,"DLSSNR.LocalToneStrength",&tone),"typed float tone");
        Check(GetFloat(p,"DLSSNR.LocalStructureStrength",&structure),"typed float structure");
        Check(GetFloat(p,"DLSSNR.SkinStructureStrength",&skin),"typed float skin");
        Check(GetInt(p,"DLSSNR.UseAutoMask",&mask),"typed signed mask");
        Check(GetUInt(p,"DLSSNR.Style",&style)&&GetUInt(p,"DLSSNR.UICorrection",&ui),"typed unsigned style/UI");
        Check(GetInt(p,"DLSSNR.Reset",&reset),"typed Reset");
        Check(GetUInt(p,"Width",&width)&&GetUInt(p,"Height",&height),"actual work extent");
        Check(GetInt(p,"DLSS.Feature.Create.Flags",&flags),"actual creation flags");
        Check(GetFloat(p,"DLSS.Pre.Exposure",&exposure),"actual canonical exposure");
        Check(GetFloat(p,"DLSSNR.MVecScaleX",&mvx)&&GetFloat(p,"DLSSNR.MVecScaleY",&mvy),"actual bound MV scale");
        Check(std::abs(intensity-wanted)<1e-6f,"real model parameter equals requested current setting");
        last_reset=reset;
    }
    std::fprintf(events,"{\"event\":\"%s\",\"segment\":%u,\"handle\":\"%p\",\"result\":%u,\"intensity\":%.9g,\"tone\":%.9g,\"structure\":%.9g,\"skin\":%.9g,\"mask\":%d,\"style\":%u,\"ui\":%u,\"reset\":%d,\"width\":%u,\"height\":%u,\"flags\":%d,\"exposure\":%.9g,\"mvx\":%.9g,\"mvy\":%.9g}\n",event,segment,handle,result,intensity,tone,structure,skin,mask,style,ui,reset,width,height,flags,exposure,mvx,mvy);
    std::fflush(events);
}
NgxResult Create(void* fn,ID3D12GraphicsCommandList* list,int feature,void* params,void** handle){
    Check(feature==18,"real bridge creates only Feature18");
    const auto result=original_create(fn,list,feature,params,handle);++creates;
    Record("create",static_cast<const NgxParams*>(params),*handle,result);return result;
}
NgxResult Evaluate(void* fn,ID3D12GraphicsCommandList* list,const void* handle,const void* params,void* callback){
    const auto result=original_evaluate(fn,list,handle,params,callback);++evaluations;
    Record("evaluate",static_cast<const NgxParams*>(params),handle,result);
    Ngx(result,"actual signed Feature18 Evaluate");
    Check(last_output==nullptr,"one NR Evaluate per source frame");
    Check(GetResource(static_cast<const NgxParams*>(params),"DLSSNR.Output",&last_output)&&last_output,"real complete N resource");
    ID3D12Resource_AddRef(last_output);return result;
}
NgxResult Release(void* fn,void* handle){
    const auto result=original_release(fn,handle);++releases;Record("release",nullptr,handle,result);return result;
}
void Run(const wchar_t* directory,const wchar_t* mode){
    queue_test::Fixture f;swprintf_s(g_log_path,L"%ls\\core.log",directory);swprintf_s(g_ini_path,L"%ls\\host.ini",directory);
    wchar_t path[MAX_PATH];swprintf_s(path,L"%ls\\events.jsonl",directory);events=_wfopen(path,L"wb");Check(events!=nullptr,"events file");
    DXGI_ADAPTER_DESC desc{};Hr(IDXGIAdapter_GetDesc(f.adapter,&desc),"adapter identity");
    std::printf("NVIDIA %ls luid=%08lx:%08lx\n",desc.Description,desc.AdapterLuid.HighPart,desc.AdapterLuid.LowPart);
    g_config=ChainConfig{};g_config.enabled=true;g_config.mode=2;g_config.processing_start=nr_processing::Start::Before;g_config.processing_selection=1;
    g_config.warmup_after_create=2;g_config.transfer_strength=1;g_config_generation=1;g_requested_mode_fast=2;
    nr_processing_runtime::Reconcile();Check(EnsureNrApi(f.device),"actual signed NR bootstrap");Check(EnsureCodec(f.device),"actual production CSOs");
    descriptor_warp_test::InstallCommandObservation(f);
    original_create=g_nr_api.bridge_create;original_evaluate=g_nr_api.bridge_evaluate;original_release=g_nr_api.bridge_release;
    g_nr_api.bridge_create=Create;g_nr_api.bridge_evaluate=Evaluate;g_nr_api.bridge_release=Release;
    HMODULE core=GetModuleHandleW(L"_nvngx.dll");Check(core!=nullptr,"official NGX core");
    auto create=(CreateFeatureFn)GetProcAddress(core,"NVSDK_NGX_D3D12_CreateFeature");
    auto evaluate=(EvaluateFeatureFn)GetProcAddress(core,"NVSDK_NGX_D3D12_EvaluateFeature");
    auto release=(ReleaseFeatureFn)GetProcAddress(core,"NVSDK_NGX_D3D12_ReleaseFeature");Check(create&&evaluate&&release,"actual SR exports");
    NgxParams* params=nullptr;Ngx(g_nr_api.allocate(&params),"allocate SR parameters");
    for(auto pair:{std::pair{"Width",320u},{"Height",192u},{"OutWidth",640u},{"OutHeight",384u},{"CreationNodeMask",1u},{"VisibilityNodeMask",1u}})SetUInt(params,pair.first,pair.second);
    SetInt(params,"PerfQualityValue",0);SetInt(params,"DLSS.Feature.Create.Flags",2);
    NgxHandle* handle=nullptr;{queue_test::List list(f);Ngx(create(list.native,1,params,&handle),"real SR Create");post_output_test::Submit(f,list);}RememberFeature(handle,1,params);
    auto* color=Plane(f,320,192,DXGI_FORMAT_R16G16B16A16_FLOAT,false);auto* depth=Plane(f,320,192,DXGI_FORMAT_R32_FLOAT,false);
    auto* motion=Plane(f,320,192,DXGI_FORMAT_R16G16_FLOAT,false);auto* output=Plane(f,640,384,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    for(auto entry:{std::pair{L"input-color.rgba16",color},std::pair{L"input-depth.r32",depth},std::pair{L"input-motion.rg16",motion}}){
        auto state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;const auto bytes=post_output_test::ReadTexture(f,entry.second,state);
        swprintf_s(path,L"%ls\\%ls",directory,entry.first);FILE* file=_wfopen(path,L"wb");Check(file!=nullptr,"fixed model source capture");
        Check(fwrite(bytes.data(),1,bytes.size(),file)==bytes.size(),"complete fixed source capture");fclose(file);
    }
    SetResource(params,"Color",color);SetResource(params,"Depth",depth);SetResource(params,"MotionVectors",motion);SetResource(params,"Output",output);
    SetUInt(params,"DLSS.Render.Subrect.Dimensions.Width",320);SetUInt(params,"DLSS.Render.Subrect.Dimensions.Height",192);
    SetFloat(params,"MV.Scale.X",1);SetFloat(params,"MV.Scale.Y",1);SetFloat(params,"Jitter.Offset.X",0);SetFloat(params,"Jitter.Offset.Y",0);
    SetFloat(params,"DLSS.Pre.Exposure",1);SetFloat(params,"DLSS.Exposure.Scale",1);SetFloat(params,"DLSS.FrameTimeDeltaInMsec",16.6667f);
    const bool hot=std::wcscmp(mode,L"aba")==0;const unsigned segments=hot?3:1;
    for(segment=0;segment<segments;++segment){
        wanted=(hot?segment==1:std::wcscmp(mode,L"b")==0)?1.7f:1.2f;g_config.intensity=wanted;++g_config_generation;
        const unsigned before_create=creates,before_release=releases;unsigned count=0;
        for(unsigned attempt=0;attempt<84&&count<10;++attempt){
            queue_test::List list(f);auto snapshot=std::make_unique<NativeCommandState>();
            (void)CaptureNativeCommandState(list.native,*snapshot);ReleaseNativeCommandState(*snapshot);
            list.Close();list.ResetWithNewAllocator(f);ID3D12GraphicsCommandList_SetPipelineState(list.native,g_codec.encode);
            Check(CaptureNativeCommandState(list.native,*snapshot),"actual observed Reset/PSO");ReleaseNativeCommandState(*snapshot);
            EnsurePostBoundaryNativeHooks(list.native);nr_source_runtime::SeedCoreRecordingDevice(list.native);
            auto state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;Transition(list.native,output,state,D3D12_RESOURCE_STATE_COPY_DEST);Transition(list.native,output,state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            SetInt(params,"Reset",attempt==0?1:0);SetInt(params,"DLSS.Reset",attempt==0?1:0);
            Ngx(DispatchEvaluate(evaluate,list.native,handle,params,nullptr),"real normal Core NR then SR");
            list.Close();list.Submit(f.a);f.Drain(f.a);
            if(last_output){
                Check(count!=0||last_reset==1,"cold and replacement NR begin with real Reset");
                NrSlot* owned=nullptr;for(auto& context:g_contexts)for(auto& slot:context.slots)if(slot.neural==last_output)owned=&slot;
                Check(owned!=nullptr,"read actual model N owned by live context");Check(IsSlotFenceCompleteLocked(*owned),"actual submitted N completed before readback");
                const auto bytes=post_output_test::ReadTexture(f,last_output,owned->neural_state);
                const auto d=ID3D12Resource_GetDesc(last_output);Check(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT&&bytes.size()==d.Width*d.Height*8,"complete FP16 N plane");
                swprintf_s(path,L"%ls\\s%u-n%02u.rgba16",directory,segment,count);FILE* file=_wfopen(path,L"wb");Check(file!=nullptr,"N capture file");Check(fwrite(bytes.data(),1,bytes.size(),file)==bytes.size(),"full N written");fclose(file);
                std::fprintf(events,"{\"event\":\"capture\",\"segment\":%u,\"frame\":%u,\"attempt\":%u,\"reset\":%d,\"width\":%llu,\"height\":%u,\"bytes\":%zu}\n",segment,count,attempt,last_reset,d.Width,d.Height,bytes.size());std::fflush(events);
                ReleaseCom(last_output);++count;
            }
            Hr(ID3D12CommandAllocator_Reset(list.replacement),"completed allocator reset");Hr(ID3D12GraphicsCommandList_Reset(list.native,list.replacement,g_codec.encode),"retire source recording");list.Close();list.Detach();
        }
        Check(count==10,"ten actual model frames per aligned segment");Check(creates==before_create+1,"normal Core creates exactly one model for changed setting");
        if(segment)Check(releases==before_release+1,"normal Core releases replaced model after completed work");
        std::printf("segment=%u intensity=%.3f captures=%u creates=%u releases=%u\n",segment,wanted,count,creates,releases);
    }
    fclose(events);events=nullptr;std::printf("PASS REAL_MODEL_BOUNDARY checks=%u; static synthetic inputs; output comparison is runner evidence, not assumed equality\n",queue_test::checks);
    // Process exit owns final vendor teardown. No in-flight GPU work remains.
}
}
