// Includes the frozen complete production Core. GPU resources are artificial,
// but Initialize/Create/Evaluate/Release use the actual signed NVIDIA runtime.
#ifndef NR_PRESENT_NGX_CORE
#error Supply the absolute frozen nr_before_sr.cpp path through NR_PRESENT_NGX_CORE.
#endif
#include NR_PRESENT_NGX_CORE
#include <dxgi1_4.h>
#include <string>
#include <vector>
#include <stdexcept>

namespace host_ngx {
unsigned checks=0;
void Check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
void Hr(HRESULT result,const char* label){if(FAILED(result))std::fprintf(stderr,"HRESULT %s=0x%08lX\n",label,static_cast<unsigned long>(result));Check(SUCCEEDED(result),label);}
void BoundContract(const NgxParams* params,const NrTuningSignature& tuning,unsigned width,unsigned height){
    int reset=0;Check(GetInt(params,"DLSSNR.Reset",&reset)&&reset==1,"color-only model has Reset on every bound frame");
    for(const auto* key:{"DLSSNR.InputWidth","DLSSNR.OutputWidth","DLSSNR.DepthSubrectWidth","DLSSNR.MVecSubrectWidth"}){
        unsigned value=0;Check(GetUInt(params,key,&value)&&value==width,"bound common raster width");}
    for(const auto* key:{"DLSSNR.InputHeight","DLSSNR.OutputHeight","DLSSNR.DepthSubrectHeight","DLSSNR.MVecSubrectHeight"}){
        unsigned value=0;Check(GetUInt(params,key,&value)&&value==height,"bound common raster height");}
    const char* keys[]={"DLSSNR.Intensity","DLSSNR.LocalToneStrength","DLSSNR.LocalStructureStrength"};
    const float expected[]={tuning.intensity,tuning.local_tone,tuning.local_structure};
    for(unsigned i=0;i<3;++i){float value=0;Check(GetFloat(params,keys[i],&value)&&value==expected[i],"actual NGX block preserves independent layer tuning");}
    ID3D12Resource *depth=nullptr,*motion=nullptr;
    Check(GetResource(params,"DLSSNR.Depth",&depth)&&depth&&GetResource(params,"DLSSNR.MVec",&motion)&&motion,"actual bound placeholder resources");
    const auto d=ID3D12Resource_GetDesc(depth),m=ID3D12Resource_GetDesc(motion);
    Check(d.Width==width&&d.Height==height&&d.Format==DXGI_FORMAT_R32_FLOAT&&
        m.Width==width&&m.Height==height&&m.Format==DXGI_FORMAT_R16G16_FLOAT,"typed placeholders use common model grid");
}
void PlaceholderPixels(const nr_replay::Header& header,const std::vector<unsigned char>& payload){
    for(auto role:{nr_replay::Role::Depth,nr_replay::Role::Motion}){
        const auto& plane=header.planes[unsigned(role)];const bool depth=role==nr_replay::Role::Depth;
        Check(plane.width==header.work_width&&plane.height==header.work_height&&plane.channels==(depth?1u:2u),"captured placeholder common grid");
        const bool half=plane.encoding==unsigned(nr_replay::Encoding::Float16);const unsigned stride=half?2u:4u;
        const auto samples=std::uint64_t(plane.width)*plane.height*plane.channels;
        for(std::uint64_t i=0;i<samples;++i){std::uint32_t value=0;std::memcpy(&value,payload.data()+plane.offset+i*stride,stride);
            Check(value==(depth?(half?0x3c00u:0x3f800000u):0u),"actual GPU placeholder depth is one and motion is zero");}
    }
}
void Wait(ID3D12Fence* fence,unsigned long long value){
    Check(fence&&value,"real nonzero fence");HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event!=nullptr,"fence event");
    Hr(ID3D12Fence_SetEventOnCompletion(fence,value,event),"fence completion subscription");
    const auto result=WaitForSingleObject(event,60000);CloseHandle(event);Check(result==WAIT_OBJECT_0,"bounded NVIDIA completion");
    const auto done=ID3D12Fence_GetCompletedValue(fence);Check(done!=UINT64_MAX&&done>=value,"NVIDIA exact fence completed");
}
struct Io {
    ID3D12Device* device=nullptr;ID3D12CommandQueue* queue=nullptr;
    ID3D12CommandAllocator* allocator=nullptr;ID3D12GraphicsCommandList* list=nullptr;ID3D12Fence* fence=nullptr;
    ID3D12Resource *backbuffer=nullptr,*upload=nullptr,*readback=nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};unsigned long long bytes=0,serial=0;
    static constexpr unsigned width=640,height=360;
    std::vector<unsigned> reference,pixels;
    void (*observed_execute)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*)=nullptr;
    void Buffer(D3D12_HEAP_TYPE type,ID3D12Resource** out){D3D12_HEAP_PROPERTIES h{};h.Type=type;h.CreationNodeMask=h.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Hr(ID3D12Device_CreateCommittedResource(device,&h,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(out)),"IO buffer");}
    void Init(ID3D12Device* d,ID3D12CommandQueue* q){device=d;queue=q;
        Hr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,reinterpret_cast<void**>(&allocator)),"IO allocator");
        Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_ID3D12GraphicsCommandList,reinterpret_cast<void**>(&list)),"IO list");
        Hr(ID3D12GraphicsCommandList_Close(list),"initial IO close");
        Hr(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(&fence)),"IO fence");
        D3D12_RESOURCE_DESC texture{};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;texture.Width=width;texture.Height=height;
        texture.DepthOrArraySize=texture.MipLevels=texture.SampleDesc.Count=1;texture.Format=DXGI_FORMAT_R8G8B8A8_UNORM;texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        Hr(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_PRESENT,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&backbuffer)),"artificial backbuffer");
        ID3D12Device_GetCopyableFootprints(device,&texture,0,1,0,&footprint,nullptr,nullptr,&bytes);Buffer(D3D12_HEAP_TYPE_UPLOAD,&upload);Buffer(D3D12_HEAP_TYPE_READBACK,&readback);
        void* mapped=nullptr;D3D12_RANGE none{0,0};Hr(ID3D12Resource_Map(upload,0,&none,&mapped),"map gradient");reference.resize(width*height);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            const unsigned pixel=0xff000000u|((x*255/width)<<16)|((y*255/height)<<8)|64u;reference[y*width+x]=pixel;
            std::memcpy(static_cast<char*>(mapped)+y*footprint.Footprint.RowPitch+x*4,&pixel,4);
        }ID3D12Resource_Unmap(upload,0,nullptr);
    }
    void Reset(){Hr(ID3D12CommandAllocator_Reset(allocator),"reset IO allocator");Hr(ID3D12GraphicsCommandList_Reset(list,allocator,nullptr),"reset IO list");}
    void Submit(){Hr(ID3D12GraphicsCommandList_Close(list),"close IO list");ID3D12CommandList* commands[]={reinterpret_cast<ID3D12CommandList*>(list)};
        if(observed_execute)observed_execute(queue,1,commands);else ID3D12CommandQueue_ExecuteCommandLists(queue,1,commands);
        Hr(ID3D12CommandQueue_Signal(queue,fence,++serial),"IO signal");Wait(fence,serial);}
    void Fill(){Reset();auto state=D3D12_RESOURCE_STATE_PRESENT;Transition(list,backbuffer,state,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=backbuffer;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        ID3D12GraphicsCommandList_CopyTextureRegion(list,&to,0,0,0,&from,nullptr);Transition(list,backbuffer,state,D3D12_RESOURCE_STATE_PRESENT);Submit();}
    unsigned Read(){Reset();auto state=D3D12_RESOURCE_STATE_PRESENT;Transition(list,backbuffer,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=backbuffer;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
        ID3D12GraphicsCommandList_CopyTextureRegion(list,&to,0,0,0,&from,nullptr);Transition(list,backbuffer,state,D3D12_RESOURCE_STATE_PRESENT);Submit();
        D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};void* mapped=nullptr;Hr(ID3D12Resource_Map(readback,0,&range,&mapped),"map presented result");
        pixels.resize(width*height);unsigned changed=0;for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){unsigned pixel=0;std::memcpy(&pixel,static_cast<char*>(mapped)+y*footprint.Footprint.RowPitch+x*4,4);pixels[y*width+x]=pixel;changed+=(pixel&0xffffff)!=(reference[y*width+x]&0xffffff);}
        D3D12_RANGE none{0,0};ID3D12Resource_Unmap(readback,0,&none);return changed;
    }
    void Destroy(){ReleaseCom(backbuffer);ReleaseCom(upload);ReleaseCom(readback);ReleaseCom(fence);ReleaseCom(list);ReleaseCom(allocator);}
};
}
int wmain(int argc,wchar_t** argv){
    using namespace host_ngx;namespace present=nr_compat_present_runtime;
    std::setvbuf(stdout,nullptr,_IONBF,0);
    try {
        Check(argc==4||argc==5,"usage: host directory passes percent [--preset-zero|--temporal-adaptive|--temporal-low-frequency]");
        const bool preset_zero=argc==5&&std::wcscmp(argv[4],L"--preset-zero")==0;
        const bool temporal=argc==5&&(std::wcscmp(argv[4],L"--temporal-adaptive")==0||std::wcscmp(argv[4],L"--temporal-low-frequency")==0);
        Check(argc==4||preset_zero||temporal,"known host option");
        const bool low_frequency=temporal&&std::wcscmp(argv[4],L"--temporal-low-frequency")==0;
        const unsigned passes=std::wcstoul(argv[2],nullptr,10),scale=std::wcstoul(argv[3],nullptr,10);
        Check(passes>=1&&passes<=5&&(scale==25||scale==100||(passes>1&&(scale==50||scale==75))),"bounded scenario");
        InitializeCriticalSection(&g_chain_cs);g_config=ChainConfig{};g_config.enabled=true;g_enabled_fast.store(true);
        nr_layers::Migrate(g_config.nr_layers,passes,nr_layers::Primary(g_config));
        for(unsigned i=1;i<passes;++i){auto& layer=g_config.nr_layers[i-1];
            layer.intensity=1.0f+0.1f*i;layer.local_tone=0.8f+0.05f*i;layer.local_structure=0.7f+0.05f*i;}
        g_config.compat_percent=scale;g_config.warmup_after_create=2;NormalizeConfig(g_config);
        Check(g_config.compat_percent==scale,"normalization preserves selected Present scale");
        Check(present::RequestedPasses(g_config)==passes,"production active independent layer count");
        g_config.transfer_strength=1.0f;g_config_generation.store(1);
        swprintf_s(g_log_path,L"%ls\\present-ngx.log",argv[1]);swprintf_s(g_ini_path,L"%ls\\host.ini",argv[1]);
        IDXGIFactory4* factory=nullptr;IDXGIAdapter1* adapter=nullptr;ID3D12Device* device=nullptr;ID3D12CommandQueue* queue=nullptr;
        Hr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory)),"factory");
        for(unsigned i=0;IDXGIFactory4_EnumAdapters1(factory,i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};IDXGIAdapter1_GetDesc1(adapter,&d);
            if(d.VendorId==0x10de&&!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){std::printf("NVIDIA vendor=%x device=%x\n",d.VendorId,d.DeviceId);break;}ReleaseCom(adapter);}
        Check(adapter!=nullptr,"real NVIDIA adapter");Hr(D3D12CreateDevice(reinterpret_cast<IUnknown*>(adapter),D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void**>(&device)),"NVIDIA device");
#ifdef NR_HOST_COLD_BOOTSTRAP
        Check(GetModuleHandleW(L"_nvngx.dll")==nullptr,"cold process has no game NGX core or initialization");
#else
        std::wstring bootstrap_path=std::wstring(argv[1])+L"\\ngx-bootstrap.dll";HMODULE bootstrap=LoadLibraryW(bootstrap_path.c_str());Check(bootstrap!=nullptr,"official bootstrap DLL");
        using InitFn=unsigned(*)(ID3D12Device*,const wchar_t*);const auto init=reinterpret_cast<InitFn>(GetProcAddress(bootstrap,"HostInitializeNgx"));Check(init!=nullptr,"official bootstrap export");
        const auto initialized=init(device,argv[1]);std::printf("official-init=0x%08X coreLoaded=%d\n",initialized,GetModuleHandleW(L"_nvngx.dll")!=nullptr);Check(initialized==1,"official SDK real initialization");
#endif
        Check(EnsureNrApi(device),"production EnsureNrApi");Check(EnsureCodec(device),"production EnsureCodec");
#ifdef NR_HOST_COLD_BOOTSTRAP
        const auto cold=SnapshotRuntimeLoadEvidence();
        Check(cold.bootstrap_attempted&&cold.bootstrap_succeeded&&cold.bootstrap_result==1,"production cold SDK bootstrap receipt");
        std::puts("production cold bootstrap succeeded; host Init calls=0; Feature1 creations=0");
#endif
        D3D12_COMMAND_QUEUE_DESC q{};Hr(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,reinterpret_cast<void**>(&queue)),"private real queue");
        Io io;io.Init(device,queue);auto* c=new present::Context;c->device=device;ID3D12Device_AddRef(device);c->queue=queue;ID3D12CommandQueue_AddRef(queue);
        c->source={Io::width,Io::height};c->work=nr_compat_present::WorkExtent(c->source,scale);
        c->format=DXGI_FORMAT_R8G8B8A8_UNORM;c->encoding=nr_compat_present::Encoding::Srgb;
        c->passes=passes;c->numerator=1;c->denominator=1;c->generation=1;c->tuning=CurrentNrTuningSignature();c->tuning.input_filter=0;
        for(unsigned i=0;i<passes;++i){c->layer_tuning[i]=CurrentNrTuningSignature(i);c->layer_tuning[i].input_filter=0;
            for(unsigned j=0;j<i;++j)Check(!SameNrTuningSignature(c->layer_tuning[i],c->layer_tuning[j]),"distinct independent layer requests");}
        std::printf("scenario passes=%u scale=%u Build\n",passes,scale);Check(present::Build(*c),"production Present Build");present::current=c;present::has_context.store(true);
        // Test-only comparison with Opti 8802b2b4 Config.h's explicit default 0.
        // This runs before actual Feature18 Create, never scans unknown values,
        // and does not add a production config/UI field or change the Core.
        if(preset_zero)for(unsigned i=0;i<passes;++i){
            auto* params=c->layers[i].context.params;SetUInt(params,"DLSSNR.Hint.Render.Preset",0);
            unsigned adopted_preset=99;
            Check(GetUInt(params,"DLSSNR.Hint.Render.Preset",&adopted_preset)&&adopted_preset==0,"explicit known preset written before Create");
        }
        std::printf("MODEL_PRESET=%s; fixed test-only Create contract\n",preset_zero?"explicit-0":"unspecified");
        for(unsigned i=0;i<passes;++i){const auto& model=c->layers[i].context;
            Check(model.width==c->work.width&&model.height==c->work.height,"every model uses the common work raster");
            Check(model.tuning_signature_initialized&&SameNrTuningSignature(model.tuning_signature,c->layer_tuning[i]),"production model receives its independent tuning");}
        if(temporal){
            // These negatives reach the actual consumer with no recorded work.
            auto& f=c->flights[0];g_config.temporal_backend=0;
            Check(present::RecordTemporal(*c,f)&&!c->temporal,"off adds no temporal resource");
            g_config.temporal_backend=1;
            Check(present::RecordTemporal(*c,f)&&nr_temporal_status::state.load()==19&&!c->temporal,"Present declines unproven native motion");
            g_config.temporal_backend=2;g_config.temporal_scope=1;
            Check(present::RecordTemporal(*c,f)&&nr_temporal_status::state.load()==18&&!c->temporal,"Present declines later-layer scope");
            g_config.temporal_scope=0;c->encoding=nr_compat_present::Encoding::Unknown;
            Check(present::RecordTemporal(*c,f)&&nr_temporal_status::state.load()==10&&!c->temporal,"Present declines unknown color");
            c->encoding=nr_compat_present::Encoding::Srgb;
            nr_source_runtime::present_lease={};
            Check(present::RecordTemporal(*c,f)&&nr_temporal_status::state.load()==22&&!c->temporal,"missing exact source does not launch flow");
            g_config.temporal_mode=low_frequency?1:0;
        }
        unsigned adopted=0,changed_frames=0;bool capture_requested=false;
        const unsigned target=temporal?32:4;
        for(unsigned attempt=1;attempt<=target+20&&adopted<target;++attempt){
            if(adopted==2&&!capture_requested){Check(present::nr_present_capture::RequestCapture(),"real Present capture request");capture_requested=true;}
            io.Fill();const auto done=present::Collect(*c);Check(done!=UINT64_MAX,"private Collect no device failure");const int slot=c->timeline.Free(done);Check(slot>=0,"free private flight");
            auto& f=c->flights[slot];Hr(ID3D12CommandAllocator_Reset(f.allocator),"reset private allocator");Hr(ID3D12GraphicsCommandList_Reset(f.list,f.allocator,nullptr),"reset private list");
#if NR_HAS_TEMPORAL_RUNTIME
            present::EndTemporalRecording(*c,f);
#endif
            if(temporal){
                // Controlled receipt for the exact artificial color just uploaded.
                // This validates the production consumer, not game source discovery.
                auto& receipt=nr_source_runtime::present_lease;receipt={};
                receipt.source={41,51,attempt*3ull};receipt.source_id=receipt.source.sequence;
                receipt.role=nr_source_frame::Role::Application;receipt.color_only=true;
                receipt.color_lineage_proven=true;receipt.output_width=Io::width;receipt.output_height=Io::height;
            }
            std::array<unsigned,5> before_evaluations{};std::array<bool,5> had_handle{};
            for(unsigned i=0;i<passes;++i){before_evaluations[i]=c->layers[i].context.evaluations;had_handle[i]=c->layers[i].context.nr_handle!=nullptr;}
            bool ok=false;if(!c->layers[0].context.nr_handle)ok=present::Bootstrap(*c,slot,attempt);
            else{f.backbuffer=io.backbuffer;ID3D12Resource_AddRef(f.backbuffer);D3D12_RENDER_TARGET_VIEW_DESC r{};r.Format=c->format;r.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
                ID3D12Device_CreateRenderTargetView(device,io.backbuffer,&r,ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(f.rtv));
                ok=present::Frame(*c,slot,io.backbuffer,attempt);}
            std::printf("frame=%u ok=%d status=%s result=0x%08X adopted=%d fence=%llu\n",attempt,ok,nr_compat_present::Name(present::observation.status),present::observation.result,f.adopted,f.fence);
            Check(ok,"production Bootstrap/Frame result");const bool used=f.adopted;Wait(c->fence,f.fence);present::Collect(*c);
            for(unsigned i=0;i<passes;++i){const auto& layer=c->layers[i];
                if(!had_handle[i]&&layer.context.nr_handle)BoundContract(layer.context.params,c->layer_tuning[i],c->work.width,c->work.height);
                if(layer.context.evaluations>before_evaluations[i])BoundContract(layer.evaluation_params[slot],c->layer_tuning[i],c->work.width,c->work.height);}
            if(capture_requested)std::printf("capture status=%u active=%llu consumed=%llu complete=%d submitted=%d empty=%d worker=%u remaining=%u\n",
                present::nr_present_capture::status.load(),present::nr_present_capture::active.token,present::nr_present_capture::consumed,
                present::nr_present_capture::active.finished,present::nr_present_capture::active.submitted,present::nr_present_capture::Empty(),nr_dual_capture::workers.load(),present::nr_present_capture::remaining_exports.load());
            const unsigned changed=io.Read();std::printf("readback frame=%u changed=%u\n",attempt,changed);
            if(used){++adopted;changed_frames+=changed!=0;}
        }
        Check(adopted==target&&changed_frames==target,"all actual adopted NVIDIA outputs differ from original");
        if(temporal){
            Check(c->temporal&&nr_temporal_status::recorded.load()>=target&&nr_temporal_status::submitted.load()>=target,
                "actual Present consumer records and submits temporal work");
            Check(!c->temporal->releasable(),"completed GPU still retains live recording references");
            const auto gpu=c->temporal->stats();
            Check(gpu.gpu_configuration==g_config_generation.load()&&gpu.gpu_owner==c->generation,
                "Present GPU decisions belong to the submitted configuration and owner");
            Check(gpu.gpu_frames>0&&gpu.gpu_tested>0&&gpu.gpu_adopted>0,
                "Present actually adopts sampled history after optical-flow warmup");
            std::printf("TEMPORAL mode=%u recorded=%llu submitted=%llu retainedBytes=%llu sampledTested=%llu sampledAdopted=%llu sampledRejected=%llu; actual consumer, controlled source receipt\n",
                g_config.temporal_mode,nr_temporal_status::recorded.load(),nr_temporal_status::submitted.load(),gpu.bytes,
                gpu.gpu_tested,gpu.gpu_adopted,gpu.gpu_rejected);
        }
        const auto capture_deadline=GetTickCount64()+10000;
        while((nr_dual_capture::workers.load()||present::nr_present_capture::status.load()==1)&&GetTickCount64()<capture_deadline){present::nr_present_capture::Collect();Sleep(2);}
        Check(capture_requested&&present::nr_present_capture::status.load()==2&&present::nr_present_capture::remaining_exports.load()==0,
            "requested real Present capture exported every layer after exact fence");
        wchar_t documents[768]{};Hr(SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,documents),"capture documents");
        const auto request=present::nr_present_capture::consumed;Check(request!=0,"capture consumed real request");
        std::vector<unsigned char> previous_neural;
        for(unsigned layer=0;layer<passes;++layer){
            wchar_t path[1400]{},output[1400]{},metadata[1400]{};
            swprintf_s(path,L"%ls\\DLSS5-Present-Capture\\%lu-%llu-%llu\\observed-frame.nrf",documents,GetCurrentProcessId(),request,(request<<8)|layer);
            HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            Check(file!=INVALID_HANDLE_VALUE,"actual exported NRF exists");
            nr_replay::Header header{};DWORD got=0;Check(ReadFile(file,&header,sizeof(header),&got,nullptr)&&got==sizeof(header),"actual NRF header read");
            LARGE_INTEGER size{};Check(GetFileSizeEx(file,&size)&&static_cast<unsigned long long>(size.QuadPart)==sizeof(header)+header.payload_bytes,"actual NRF complete payload size");
            Check(nr_replay::ValidHeader(header)&&header.completion==unsigned(nr_replay::Completion::Complete),"actual NRF ABI/complete header");
            Check(header.mode==c->layers[layer].context.mode&&header.instance==c->layers[layer].context.instance_id&&header.parameter_count>0,"actual private mode instance and bound parameter snapshot");
            Check(header.work_width==c->layers[layer].context.width&&header.work_height==c->layers[layer].context.height&&header.source_width==Io::width&&header.source_height==Io::height,"actual NRF source and per-layer extents");
            for(auto role:{nr_replay::Role::Source,nr_replay::Role::Proxy,nr_replay::Role::Neural,nr_replay::Role::Composite,nr_replay::Role::Depth,nr_replay::Role::Motion})
                Check(header.planes[unsigned(role)].bytes!=0,"all actual source model guide and final composition planes exported");
            std::vector<unsigned char> payload(static_cast<std::size_t>(header.payload_bytes));
            Check(ReadFile(file,payload.data(),static_cast<DWORD>(payload.size()),&got,nullptr)&&got==payload.size(),"actual GPU capture payload read");CloseHandle(file);
            PlaceholderPixels(header,payload);
            const auto& input=header.planes[unsigned(nr_replay::Role::Proxy)];const auto& neural=header.planes[unsigned(nr_replay::Role::Neural)];
            Check(input.bytes==neural.bytes&&input.encoding==neural.encoding&&std::memcmp(payload.data()+input.offset,payload.data()+neural.offset,static_cast<std::size_t>(input.bytes))!=0,"actual signed NR captured output differs from captured model input");
            if(layer)Check(input.bytes==previous_neural.size()&&
                std::memcmp(payload.data()+input.offset,previous_neural.data(),previous_neural.size())==0,
                "next actual NR input equals the entire preceding output at the shared auxiliary raster");
            previous_neural.assign(payload.begin()+neural.offset,payload.begin()+neural.offset+neural.bytes);
            for(auto role:{nr_replay::Role::Proxy,nr_replay::Role::Neural,nr_replay::Role::Composite}){
                const auto& plane=header.planes[unsigned(role)];
                swprintf_s(output,L"%ls\\plane-p%u-s%u-layer%u-role%u.bin",argv[1],passes,scale,layer+1,unsigned(role));
                HANDLE plane_file=CreateFileW(output,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
                Check(plane_file!=INVALID_HANDLE_VALUE,"create raw observed plane evidence");DWORD written=0;
                const bool wrote=WriteFile(plane_file,payload.data()+plane.offset,static_cast<DWORD>(plane.bytes),&written,nullptr)&&written==plane.bytes;
                CloseHandle(plane_file);Check(wrote,"write exact observed FP16 plane without NRF metadata");
            }
            swprintf_s(output,L"%ls\\capture-p%u-s%u-layer%u.nrf",argv[1],passes,scale,layer+1);Check(CopyFileW(path,output,TRUE)!=FALSE,"copy capture into immutable test evidence");
            swprintf_s(metadata,L"%ls\\DLSS5-Present-Capture\\%lu-%llu-%llu\\metadata.json",documents,GetCurrentProcessId(),request,(request<<8)|layer);
            swprintf_s(output,L"%ls\\capture-p%u-s%u-layer%u.json",argv[1],passes,scale,layer+1);Check(CopyFileW(metadata,output,TRUE)!=FALSE,"copy independent capture role metadata");
        }
        for(unsigned i=0;i<passes;++i){Check(c->layers[i].context.nr_handle&&c->layers[i].context.evaluations>0,"every real layer evaluated");
            for(unsigned j=0;j<i;++j)Check(c->layers[i].context.nr_handle!=c->layers[j].context.nr_handle&&c->layers[i].context.params!=c->layers[j].context.params,"independent real layer identity");
            std::printf("layer=%u instance=%llu evaluations=%u\n",i+1,c->layers[i].context.instance_id,c->layers[i].context.evaluations);}
        const auto submitted=present::observation.submitted;g_config.enabled=false;g_enabled_fast.store(false);
        for(unsigned i=0;i<4;++i)present::OnPresent(nullptr,nullptr,nullptr,nullptr,0,nullptr);
        Check(present::observation.submitted==submitted&&!present::current,"F6-disabled callback starts no work and retires exact-completed context");
        std::printf("PASS REAL_NVIDIA_PRESENT checks=%u passes=%u scale=%u adopted=%u changed=%u F6Stopped=1; artificial input, no game/FG claim\n",checks,passes,scale,adopted,changed_frames);
        io.Destroy();ReleaseCom(queue);ReleaseCom(device);ReleaseCom(adapter);ReleaseCom(factory);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL REAL_NVIDIA_PRESENT %s\n",e.what());return 1;}
}
