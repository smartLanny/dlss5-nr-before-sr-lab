// Production FG input consumer, real NVIDIA NR; the native FG call is an
// explicitly labelled placeholder in this first transport-only host.
#define wmain unused_present_ngx_main
#include "compat_present_ngx_host.cpp"
#undef wmain
#include <fstream>

namespace host_fg {
using namespace host_ngx;
unsigned injected_evaluations=0,injected_allocations=0;
constexpr NgxResult injected_result=0xBADF0011u;
NgxResult InjectEvaluateFailure(void*,ID3D12GraphicsCommandList*,const void*,const void*,void*){++injected_evaluations;return injected_result;}
NgxResult InjectAllocationFailure(NgxParams** output){++injected_allocations;*output=nullptr;return injected_result;}
void NativeDeviceMetadata(ID3D12Device* device){
    // Actual device declarations, not fabricated known-state admission bits.
    CommandStateMetaContract meta{};ID3D12Device5* extended=nullptr;
    const auto queried=ID3D12Device_QueryInterface(device,IID_ID3D12Device5,reinterpret_cast<void**>(&extended));
    if(queried==E_NOINTERFACE)meta.known=true;
    if(SUCCEEDED(queried)&&extended){UINT count=0;Hr(ID3D12Device5_EnumerateMetaCommands(extended,&count,nullptr),"query native meta commands");
        Check(count<=256,"bounded meta-command declarations");std::vector<D3D12_META_COMMAND_DESC> entries(count);UINT actual=count;
        if(count)Hr(ID3D12Device5_EnumerateMetaCommands(extended,&actual,entries.data()),"read native meta commands");
        Check(actual<=count,"stable native meta declarations");meta.known=true;
        for(UINT i=0;i<actual;++i){meta.initialization_dirty|=entries[i].InitializationDirtyState;meta.execution_dirty|=entries[i].ExecutionDirtyState;}
        ReleaseCom(extended);
    }
    meta.device_identity=g_next_command_device_identity.fetch_add(1);
    Hr(ID3D12Device_SetPrivateData(device,kCommandStateMetaContract,sizeof(meta),&meta),"store actual device meta receipt");
    EnsureCommandCreationObserver(device);Check(EnsureNativeCommandSignatureHook(device),"production signature observation");
}
void STDMETHODCALLTYPE Execute(ID3D12CommandQueue* q,UINT count,ID3D12CommandList*const* lists){ID3D12CommandQueue_ExecuteCommandLists(q,count,lists);}
void ObservedExecute(ID3D12CommandQueue* q,UINT count,ID3D12CommandList*const* lists){ProcessSubmittedCommandLists(0,q,count,lists);}
void Submit(Io& io){Hr(ID3D12GraphicsCommandList_Close(io.list),"close consumer recording");ID3D12CommandList* lists[]={reinterpret_cast<ID3D12CommandList*>(io.list)};
    ProcessSubmittedCommandLists(0,io.queue,1,lists);Hr(ID3D12CommandQueue_Signal(io.queue,io.fence,++io.serial),"host completion after core submission");Wait(io.fence,io.serial);}
ID3D12Resource* Guide(ID3D12Device* device,DXGI_FORMAT format){D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_DEFAULT;h.CreationNodeMask=h.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=Io::width;d.Height=Io::height;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
    d.Format=format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ID3D12Resource* result=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(device,&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&result)),"guide texture");return result;
}
void InitGuides(Io& io,ID3D12Resource* depth,ID3D12Resource* motion){
    nr_compat_present_runtime::Context init{};init.device=io.device;init.format=DXGI_FORMAT_R8G8B8A8_UNORM;init.source=init.work={Io::width,Io::height};
    init.encoding=nr_compat_present::Encoding::Srgb;
    Check(nr_compat_present_runtime::MakePipelines(init),"real guide initialization shader");ID3D12DescriptorHeap* table=nullptr;
    Check(nr_compat_present_runtime::Table(init,&table,nullptr,depth,motion),"guide initialization descriptors");
    io.Reset();auto ds=D3D12_RESOURCE_STATE_COMMON,ms=D3D12_RESOURCE_STATE_COMMON;
    Transition(io.list,depth,ds,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Transition(io.list,motion,ms,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    nr_compat_present_runtime::Utility(init,io.list,table,init.init,init.work);UavBarrier(io.list,depth);UavBarrier(io.list,motion);
    Transition(io.list,depth,ds,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(io.list,motion,ms,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);io.Submit();
    ReleaseCom(table);ReleaseCom(init.blit);ReleaseCom(init.convert);ReleaseCom(init.init);ReleaseCom(init.init_color);ReleaseCom(init.util_root);
}
#include "fg_consumer_hud_fixture.inl"
#if defined(NR_HOST_FG_CAPTURE)
#include "fg_consumer_capture_fixture.inl"
#endif
#if defined(NR_HOST_NATIVE_FG)
ID3D12Resource* Output(Io& io){
    auto desc=ID3D12Resource_GetDesc(io.backbuffer);desc.Flags|=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    ID3D12Resource* result=nullptr;Hr(ID3D12Device_CreateCommittedResource(io.device,&heap,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&result)),"native FG output");return result;
}
void SaveOutput(Io& io,ID3D12Resource* resource,const std::wstring& path,D3D12_RESOURCE_STATES original=D3D12_RESOURCE_STATE_UNORDERED_ACCESS){
    io.Reset();auto state=original;Transition(io.list,resource,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=resource;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=io.readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=io.footprint;
    ID3D12GraphicsCommandList_CopyTextureRegion(io.list,&to,0,0,0,&from,nullptr);Transition(io.list,resource,state,original);io.Submit();
    void* mapped=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(io.bytes)};Hr(ID3D12Resource_Map(io.readback,0,&range,&mapped),"native FG actual output map");
    FILE* file=nullptr;_wfopen_s(&file,path.c_str(),L"wb");Check(file!=nullptr,"native FG raw capture file");
    for(unsigned y=0;y<Io::height;++y)Check(std::fwrite(static_cast<char*>(mapped)+y*io.footprint.Footprint.RowPitch,4,Io::width,file)==Io::width,"native FG actual pixels saved");
    std::fclose(file);D3D12_RANGE none{0,0};ID3D12Resource_Unmap(io.readback,0,&none);
}
ID3D12Resource* DisableFlag(Io& io){D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=4;desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ID3D12Resource* result=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(io.device,&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&result)),"official interpolation decision buffer");return result;
}
bool ReadDisableFlag(Io& io,ID3D12Resource* flag){io.Reset();auto state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;Transition(io.list,flag,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyBufferRegion(io.list,io.readback,0,flag,0,4);Transition(io.list,flag,state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);io.Submit();
    void* mapped=nullptr;D3D12_RANGE range{0,4};Hr(ID3D12Resource_Map(io.readback,0,&range,&mapped),"official interpolation decision readback");
    const unsigned value=*static_cast<unsigned char*>(mapped);D3D12_RANGE none{0,0};ID3D12Resource_Unmap(io.readback,0,&none);
    Check(value<=1,"official FG overwrote interpolation decision sentinel");return value!=0;
}
#endif
}
int wmain(int argc,wchar_t** argv){using namespace host_ngx;using namespace host_fg;
    std::setvbuf(stdout,nullptr,_IONBF,0);
    try {
        Check(argc==4||(argc>=6&&argc<=8),"usage consumer-host runtime passes scale [nr-enabled fg-enabled hud-mode fault-mode]");const unsigned passes=std::wcstoul(argv[2],nullptr,10),scale=std::wcstoul(argv[3],nullptr,10);
        const bool nr_enabled=argc==4||std::wcstoul(argv[4],nullptr,10)!=0,fg_enabled=argc==4||std::wcstoul(argv[5],nullptr,10)!=0;
        const unsigned hud_mode=argc>6?std::wcstoul(argv[6],nullptr,10):0,fault_mode=argc>7?std::wcstoul(argv[7],nullptr,10):0;
        InitializeCriticalSection(&g_chain_cs);g_config=ChainConfig{};g_config.enabled=nr_enabled;g_enabled_fast=nr_enabled;
        g_config.processing_start=nr_processing::Start::Present;g_config.processing_selection=1;
        g_config.nr_passes=passes>1?2:1;g_config.nr_full_reference=passes>1&&scale==100;g_config.nr_full_passes=std::max(2u,passes);
        g_config.nr_second_numerator=scale;g_config.nr_second_denominator=100;g_config.warmup_after_create=2;g_config.compat_percent=100;g_config.transfer_strength=1;
        nr_processing_runtime::Reconcile();g_config_generation=1;g_original_queue_execute[0]=host_fg::Execute;
        swprintf_s(g_log_path,L"%ls\\fg-consumer-%u-%u-%u-%u-hud%u-fault%u.log",argv[1],passes,scale,nr_enabled,fg_enabled,hud_mode,fault_mode);swprintf_s(g_ini_path,L"%ls\\host.ini",argv[1]);
        IDXGIFactory4* factory=nullptr;IDXGIAdapter1* adapter=nullptr;ID3D12Device* device=nullptr;ID3D12CommandQueue* queue=nullptr;
        Hr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory)),"factory");
        for(unsigned i=0;IDXGIFactory4_EnumAdapters1(factory,i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};IDXGIAdapter1_GetDesc1(adapter,&d);
            if(d.VendorId==0x10de&&!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){std::printf("NVIDIA vendor=%x device=%x\n",d.VendorId,d.DeviceId);break;}ReleaseCom(adapter);}
        Check(adapter!=nullptr,"NVIDIA adapter");Hr(D3D12CreateDevice(reinterpret_cast<IUnknown*>(adapter),D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void**>(&device)),"NVIDIA device");
        const auto bootstrap=LoadLibraryW((std::wstring(argv[1])+L"\\ngx-bootstrap.dll").c_str());Check(bootstrap!=nullptr,"bootstrap");
        using Init=unsigned(*)(ID3D12Device*,const wchar_t*);const auto init=reinterpret_cast<Init>(GetProcAddress(bootstrap,"HostInitializeNgx"));Check(init&&init(device,argv[1])==1,"actual official SDK Init");
        Check(EnsureNrApi(device)&&EnsureCodec(device),"actual signed NR initialization");NativeDeviceMetadata(device);
        D3D12_COMMAND_QUEUE_DESC q{};Hr(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,reinterpret_cast<void**>(&queue)),"actual consumer queue");
        Io io;io.Init(device,queue);io.observed_execute=ObservedExecute;auto* depth=Guide(device,DXGI_FORMAT_R32_FLOAT);auto* motion=Guide(device,DXGI_FORMAT_R16G16_FLOAT);InitGuides(io,depth,motion);
        Check(InstallCommandStateObservation(io.list,true),"publish actual native state hooks");
        (void)EnsurePostBoundaryNativeHooks(io.list);
        auto* state=new NativeCommandState;(void)CaptureNativeCommandState(io.list,*state);ReleaseNativeCommandState(*state);
        io.Reset();ID3D12GraphicsCommandList_SetPipelineState(io.list,g_codec.encode);ID3D12GraphicsCommandList_SetComputeRootSignature(io.list,g_codec.root);
        Check(CaptureNativeCommandState(io.list,*state),"actual observed Reset/PSO state");ReleaseNativeCommandState(*state);delete state;io.Submit();
        HudFixture hud;hud.Init(io,hud_mode);
        NgxParams* params=nullptr;
        unsigned renderer_source=0;const NgxHandle* placeholder=reinterpret_cast<const NgxHandle*>(&renderer_source);
#if defined(NR_HOST_NATIVE_FG)
        using CreateFg=unsigned(*)(ID3D12GraphicsCommandList*,unsigned,unsigned,unsigned,void**,void**);
        using EvaluateFg=unsigned(*)(ID3D12GraphicsCommandList*,void*,void*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,unsigned,unsigned,unsigned long long);
        const auto create_fg=reinterpret_cast<CreateFg>(GetProcAddress(bootstrap,"HostCreateFg"));
        const auto evaluate_fg=reinterpret_cast<EvaluateFg>(GetProcAddress(bootstrap,"HostEvaluateFg"));Check(create_fg&&evaluate_fg,"official FG helper exports");
        io.Reset();void* actual_fg=nullptr;void* actual_params=nullptr;const auto created=create_fg(io.list,Io::width,Io::height,DXGI_FORMAT_R8G8B8A8_UNORM,&actual_fg,&actual_params);
        std::printf("official-FG-Create=0x%08X feature=%p params=%p\n",created,actual_fg,actual_params);
        Check(created==kNgxSuccess&&actual_fg&&actual_params,"actual official FG Create");io.Submit();
        placeholder=static_cast<NgxHandle*>(actual_fg);params=static_cast<NgxParams*>(actual_params);
        auto* real=Output(io);auto* interpolated=Output(io);auto* disable=DisableFlag(io);unsigned eligible_frames=0,fg_evaluations=0;
        SetResource(params,nr_fg_contract::real,real);SetResource(params,nr_fg_contract::generated,interpolated);
#else
        Check(g_nr_api.allocate(&params)==kNgxSuccess&&params,"real NGX parameter object");
#endif
        SetResource(params,nr_fg_contract::backbuffer,io.backbuffer);SetResource(params,nr_fg_contract::depth,depth);SetResource(params,nr_fg_contract::motion,motion);
        hud.Bind(params);
        SetFloat(params,nr_fg_contract::mv_x,1.f/Io::width);SetFloat(params,nr_fg_contract::mv_y,1.f/Io::height);
        SetFloat(params,nr_fg_contract::jitter_x,0);SetFloat(params,nr_fg_contract::jitter_y,0);SetUInt(params,nr_fg_contract::hdr,0);SetUInt(params,nr_fg_contract::inverted,0);SetUInt(params,nr_fg_contract::paused,0);
        using Set64=void(*)(NgxParams*,const char*,unsigned long long);const auto set64=ParamSlot<Set64>(params,7);Check(set64!=nullptr,"native uint64 parameter setter");
        nr_fg_runtime::Register();unsigned changed_frames=0;unsigned long long consumed=0;bool capture_requested=false;
        for(unsigned frame=0;frame<
#if defined(NR_HOST_NATIVE_FG)
            24
#else
            36&&changed_frames<4
#endif
            ;++frame){
#if defined(NR_HOST_FG_CAPTURE)
            if(nr_enabled&&fg_enabled&&!fault_mode&&changed_frames==2&&!capture_requested){Check(nr_fg_capture::RequestCapture(),"actual pre-FG capture requested after complete warm adoption");capture_requested=true;}
#endif
            hud.Fill();io.Fill();io.Reset();ID3D12GraphicsCommandList_SetPipelineState(io.list,g_codec.encode);ID3D12GraphicsCommandList_SetComputeRootSignature(io.list,g_codec.root);
            hud.Observe(io);
            auto cs=D3D12_RESOURCE_STATE_PRESENT;Transition(io.list,io.backbuffer,cs,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(io.list,io.backbuffer,cs,D3D12_RESOURCE_STATE_PRESENT);
#if defined(NR_HOST_NATIVE_FG)
            Transition(io.list,io.backbuffer,cs,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            auto hud_state=D3D12_RESOURCE_STATE_PRESENT;if(hud_mode)Transition(io.list,hud.scene.backbuffer,hud_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            auto flag_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;Transition(io.list,disable,flag_state,D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12GraphicsCommandList_CopyBufferRegion(io.list,disable,0,io.upload,0,4);Transition(io.list,disable,flag_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
#endif
            for(auto* r:{depth,motion}){auto state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;Transition(io.list,r,state,D3D12_RESOURCE_STATE_COMMON);Transition(io.list,r,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
            {nr_source_runtime::Lock lock;nr_source_runtime::lineage.DescribeSurface(nr_source_runtime::ResourceOf(io.backbuffer),1);}
            set64(params,nr_fg_contract::frame,frame);SetUInt(params,nr_fg_contract::reset,frame==0?1:0);
            const auto before=nr_fg_runtime::adopted.load();
            const auto recorded_before=nr_fg_runtime::recorded.load(),second_before=nr_dual_runtime::second_evaluations.load();
            const auto original_evaluate=g_nr_api.bridge_evaluate;const auto original_allocate=g_nr_api.allocate;
            const bool inject_opaque=fault_mode==1&&frame==4,inject_soft=fault_mode==2&&frame==0;
            if(inject_opaque)g_nr_api.bridge_evaluate=InjectEvaluateFailure;
            if(inject_soft)g_nr_api.allocate=InjectAllocationFailure;
            {nr_processing_runtime::CarrierScope processing;nr_source_runtime::EvaluateScope input(io.list,placeholder,params,11,1);
                g_nr_api.bridge_evaluate=original_evaluate;g_nr_api.allocate=original_allocate;
                if(inject_opaque){Check(injected_evaluations==1&&input.FatalResult()==injected_result,"injected opaque failure propagated exactly once");
                    Check(nr_fg_runtime::adopted.load()==before&&nr_dual_runtime::second_evaluations.load()==second_before,"opaque failure has no adoption or later NR evaluation");
                    input.Finish(input.FatalResult());std::printf("PASS INJECTED_NR_OPAQUE checks=%u injectedResult=0x%08X earlierRealNR=1 nativeFGAfterFailure=0 submitFailedList=0; controlled API fault, not a driver failure\n",checks,injected_result);return 0;}
                if(inject_soft){Check(injected_allocations==1&&!input.FatalResult()&&nr_fg_runtime::recorded.load()==recorded_before,"private allocation refusal leaves native FG admissible");
                    std::printf("SOFT_NR_PREFLIGHT injectedAllocationFailure=1 fatal=0 nrRecorded=0; original native FG follows\n");}
                if(frame<4)std::printf("source stream=%llu epoch=%llu sequence=%llu active=%d recorded=%d ticket=%llu states=%d encoding=%u\n",input.source.stream,input.source.epoch,input.source.sequence,input.active,input.private_recorded,input.ticket.attempt,input.inputs.states_known,input.inputs.encoding);
                Check(!input.FatalResult(),"FG input consumer opaque boundary");
#if defined(NR_HOST_NATIVE_FG)
                if(fg_enabled){const auto generated=evaluate_fg(io.list,actual_fg,params,io.backbuffer,depth,motion,real,interpolated,disable,hud_mode?hud.scene.backbuffer:nullptr,hud.alpha,Io::width,Io::height,frame);
                    std::printf("official-FG-Evaluate frame=%u result=0x%08X\n",frame,generated);input.Finish(generated);Check(generated==kNgxSuccess,"actual official FG Evaluate");++fg_evaluations;
                }else input.Finish(kNgxSuccess);
#else
                input.Finish(kNgxSuccess); // Explicit placeholder for the original FG call.
#endif
            }
#if defined(NR_HOST_NATIVE_FG)
            Transition(io.list,io.backbuffer,cs,D3D12_RESOURCE_STATE_PRESENT);
            if(hud_mode)Transition(io.list,hud.scene.backbuffer,hud_state,D3D12_RESOURCE_STATE_PRESENT);
#endif
            host_fg::Submit(io);const unsigned changed=io.Read();consumed=nr_fg_runtime::recorded.load();
#if defined(NR_HOST_FG_CAPTURE)
            nr_fg_capture::Collect();
#endif
#if defined(NR_HOST_NATIVE_FG)
            if(fg_enabled){const bool disabled=ReadDisableFlag(io,disable);if(!disabled)++eligible_frames;std::printf("official-FG-output frame=%u disableInterpolation=%d\n",frame,disabled);}
#endif
            const unsigned actual=!nr_enabled?0:g_config.nr_full_reference?nr_dual_runtime::full_adopted.load():
                passes>1?(nr_dual_runtime::status.load()==nr_dual_runtime::Status::Active?2u:1u):1u;
            std::printf("frame=%u attempts=%llu recorded=%llu adopted=%llu pixelsChanged=%u actualPasses=%u dualStatus=%u bypass=%s state=%s\n",frame,nr_fg_runtime::attempts.load(),consumed,nr_fg_runtime::adopted.load(),changed,actual,static_cast<unsigned>(nr_dual_runtime::status.load()),BypassReasonName(static_cast<BypassReason>(g_last_bypass_reason.load())),g_command_state_evidence.reason);
            if(nr_fg_runtime::adopted.load()>before&&changed&&actual==passes){++changed_frames;hud.CheckAdoption(io);}
        }
        Check(nr_enabled?(changed_frames>=4&&consumed>0):consumed==0,"requested real NR enablement and outputs");
#if defined(NR_HOST_FG_CAPTURE)
        if(nr_enabled&&fg_enabled&&!fault_mode){Check(capture_requested,"FG capture request reached actual warm chain");ValidateFgCapture(argv[1],passes,scale,hud_mode);}
#endif
#if defined(NR_HOST_NATIVE_FG)
        const auto suffix=L"-p"+std::to_wstring(passes)+L"-s"+std::to_wstring(scale)+L"-nr"+std::to_wstring(nr_enabled)+L"-fg"+std::to_wstring(fg_enabled)+(hud_mode?L"-hud"+std::to_wstring(hud_mode):L"")+(fault_mode?L"-fault"+std::to_wstring(fault_mode):L"")+L".rgba";
        if(fg_enabled){Check(fg_evaluations==24&&eligible_frames>=4,"official FG produced at least four eligible interpolated frames");
            SaveOutput(io,real,std::wstring(argv[1])+L"\\OutputReal"+suffix);SaveOutput(io,interpolated,std::wstring(argv[1])+L"\\OutputInterpolated"+suffix);}
        SaveOutput(io,io.backbuffer,std::wstring(argv[1])+L"\\ConsumerBackbuffer"+suffix,D3D12_RESOURCE_STATE_PRESENT);
        if(hud_mode)SaveOutput(io,hud.scene.backbuffer,std::wstring(argv[1])+L"\\ConsumerHUDless"+suffix,D3D12_RESOURCE_STATE_PRESENT);
#endif
        if(nr_enabled){auto* primary=nr_fg_runtime::sessions[0].primary;Check(primary&&primary->nr_handle&&primary->evaluations>0,"primary real NR instance");
        unsigned layers=1;
        for(const auto& c:g_contexts)if(c.parent_instance_id==primary->instance_id&&!c.retired&&c.nr_handle){
            Check(c.evaluations>0&&c.nr_handle!=primary->nr_handle&&c.params!=primary->params,"actual independent auxiliary evaluation");++layers;
            std::printf("layer=%u instance=%llu parent=%llu evaluations=%u\n",c.layer_index+1,c.instance_id,c.parent_instance_id,c.evaluations);}
        Check(layers==passes,"requested layer count actually evaluated");}
        const auto before=nr_fg_runtime::recorded.load();g_config.enabled=false;g_enabled_fast=false;nr_processing_runtime::Reconcile();
        {nr_source_runtime::EvaluateScope off(io.list,placeholder,params,11,1);Check(!off.active,"disabled source scope skips consumer");}
        Check(nr_fg_runtime::recorded.load()==before,"off produces no new NR recording");
        std::printf("PASS REAL_NR_FG_CONSUMER checks=%u passes=%u scale=%u changedFrames=%u nrEnabled=%d fgEnabled=%d; %s, artificial inputs, no game claim\n",checks,passes,scale,changed_frames,nr_enabled,fg_enabled,
#if defined(NR_HOST_NATIVE_FG)
            fg_enabled?"official native FG Create/Evaluate and raw real/interpolated outputs":"native FG Evaluate disabled; actual consumer readback only"
#else
            "native-FG-placeholder"
#endif
        );return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL REAL_NR_FG_CONSUMER %s\n",e.what());return 1;}
}
