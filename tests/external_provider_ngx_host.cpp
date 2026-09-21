// Real D3D12 producer for the frozen Core ABI. Synthetic colour/depth/motion;
// no game, native SR/FG, or packaged Feeder acceptance is implied.
#define wmain unused_present_main
#include "compat_present_ngx_host.cpp"
#undef wmain

namespace host_external {
using namespace host_ngx;
constexpr std::uint64_t provider=0x45585454455354ull;
template<class T> std::uint64_t Wire(T* object){return reinterpret_cast<std::uintptr_t>(object);}
void Metadata(ID3D12Device* device){
    // Match the production device observer using real driver declarations.
    CommandStateMetaContract meta{};ID3D12Device5* extended=nullptr;
    const auto queried=ID3D12Device_QueryInterface(device,IID_ID3D12Device5,reinterpret_cast<void**>(&extended));
    if(queried==E_NOINTERFACE)meta.known=true;
    if(SUCCEEDED(queried)&&extended){UINT count=0;Hr(ID3D12Device5_EnumerateMetaCommands(extended,&count,nullptr),"meta command count");
        Check(count<=256,"bounded native declarations");std::vector<D3D12_META_COMMAND_DESC> entries(count);UINT actual=count;
        if(count)Hr(ID3D12Device5_EnumerateMetaCommands(extended,&actual,entries.data()),"native meta declarations");
        Check(actual<=count,"stable meta declarations");meta.known=true;
        for(UINT i=0;i<actual;++i){meta.initialization_dirty|=entries[i].InitializationDirtyState;meta.execution_dirty|=entries[i].ExecutionDirtyState;}
        ReleaseCom(extended);
    }
    meta.device_identity=g_next_command_device_identity.fetch_add(1);
    Hr(ID3D12Device_SetPrivateData(device,kCommandStateMetaContract,sizeof(meta),&meta),"actual device metadata receipt");
    EnsureCommandCreationObserver(device);Check(EnsureNativeCommandSignatureHook(device),"signature observer");
}
ID3D12Resource* Guide(Io& io,DXGI_FORMAT format){
    auto d=ID3D12Resource_GetDesc(io.backbuffer);d.Format=format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_DEFAULT;h.CreationNodeMask=h.VisibleNodeMask=1;
    ID3D12Resource* result=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(io.device,&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,
        IID_ID3D12Resource,reinterpret_cast<void**>(&result)),"synthetic guide resource");return result;
}
void InitGuides(Io& io,ID3D12Resource* depth,ID3D12Resource* motion){
    namespace p=nr_compat_present_runtime;
    p::Context init{};init.device=io.device;init.format=DXGI_FORMAT_R8G8B8A8_UNORM;init.source=init.work={Io::width,Io::height};
    init.encoding=nr_compat_present::Encoding::Srgb;
    Check(p::MakePipelines(init),"guide initialization pipelines");ID3D12DescriptorHeap* table=nullptr;
    ID3D12PipelineState* kernel=nullptr;auto* shader=p::CompileUtility("InitCS","cs_5_0");Check(shader,"guide initializer shader");
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=init.util_root;
    desc.CS={ID3D10Blob_GetBufferPointer(shader),ID3D10Blob_GetBufferSize(shader)};
    Hr(ID3D12Device_CreateComputePipelineState(io.device,&desc,IID_ID3D12PipelineState,reinterpret_cast<void**>(&kernel)),"guide initializer PSO");ReleaseCom(shader);
    Check(p::Table(init,&table,nullptr,depth,motion),"guide initializer descriptors");
    io.Reset();auto ds=D3D12_RESOURCE_STATE_COMMON,ms=ds;
    Transition(io.list,depth,ds,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Transition(io.list,motion,ms,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    p::Utility(init,io.list,table,kernel,init.work);UavBarrier(io.list,depth);UavBarrier(io.list,motion);
    Transition(io.list,depth,ds,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(io.list,motion,ms,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);io.Submit();
    ReleaseCom(table);ReleaseCom(kernel);ReleaseCom(init.blit);ReleaseCom(init.convert);ReleaseCom(init.init_color);ReleaseCom(init.util_root);
}
NrExternalResourceV1 Resource(ID3D12Resource* resource,D3D12_RESOURCE_STATES state){
    const auto d=ID3D12Resource_GetDesc(resource);NrExternalResourceV1 wire{};
    wire.resource=Wire(resource);wire.format=d.Format;wire.width=wire.rect_w=static_cast<unsigned>(d.Width);
    wire.height=wire.rect_h=d.Height;wire.state_before=wire.state_after=state;
    wire.provenance=NrGuideProvenance::Synthetic;return wire;
}
NrExternalStatusV1 Query(){NrExternalStatusV1 status{};
    Check(NRExternalProvider_QueryV1(kNrExternalProviderAbi,sizeof(status),0,&status)==1,"actual ABI Query");return status;}
void RecordState(Io& io){io.Reset();ID3D12GraphicsCommandList_SetPipelineState(io.list,g_codec.encode);
    ID3D12GraphicsCommandList_SetComputeRootSignature(io.list,g_codec.root);}
void SubmitBlocked(Io& io){
    // A genuine queue wait keeps submitted GPU commands in flight. The
    // separate test gate is CPU-signalled even if an assertion throws.
    ID3D12Fence* gate=nullptr;
    Hr(ID3D12Device_CreateFence(io.device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(&gate)),"bounded test gate fence");
    struct OpenGate {ID3D12Fence*& gate;~OpenGate(){if(gate){ID3D12Fence_Signal(gate,1);ReleaseCom(gate);}}} open{gate};
    Hr(ID3D12CommandQueue_Wait(io.queue,gate,1),"real GPU queue wait on test gate");
    Hr(ID3D12GraphicsCommandList_Close(io.list),"close actual producer recording");
    ID3D12CommandList* lists[]={reinterpret_cast<ID3D12CommandList*>(io.list)};
    ID3D12CommandQueue_ExecuteCommandLists(io.queue,1,lists);
    const auto value=++io.serial;Hr(ID3D12CommandQueue_Signal(io.queue,io.fence,value),"producer exact completion signal queued");
    Check(ID3D12Fence_GetCompletedValue(io.fence)<value,"submitted work remains incomplete behind real queue gate");
    Check(NRExternalProvider_ReleaseOwnershipV1(kNrExternalProviderAbi,provider)==0,"Release refuses actually submitted incomplete GPU work");
    Check(Query().owner_provider_id==provider&&nr_external_provider_feedback::pending_completions.load()!=0,"Query retains the submitted incomplete receipt");
    nr_fg_runtime::Session* session=nullptr;
    for(auto& candidate:nr_fg_runtime::sessions)if(candidate.stream){session=&candidate;break;}
    Check(session&&session->primary&&session->images.raw,"in-flight model and transport exist");
    auto* raw=session->images.raw;auto* primary=session->primary;
    EnterCriticalSection(&g_chain_cs);const bool releasable=CanDestroyContextLocked(*primary);LeaveCriticalSection(&g_chain_cs);
    Check(!releasable,"real incomplete submission blocks model destruction");
    nr_fg_runtime::CollectRetired();Check(session->images.raw==raw&&session->primary==primary,"in-flight resources survive collection");
    Hr(ID3D12Fence_Signal(gate,1),"open bounded test gate");Wait(io.fence,value);
}
}

int wmain(int argc,wchar_t** argv){using namespace host_ngx;using namespace host_external;
    std::setvbuf(stdout,nullptr,_IONBF,0);
    try{
        Check(argc==3,"usage external-provider-host evidence-directory layers");const unsigned layers=std::wcstoul(argv[2],nullptr,10);
        Check(layers==1||layers==2,"bounded one/two layer scenarios");
        InitializeCriticalSection(&g_chain_cs);g_config=ChainConfig{};g_config.enabled=true;g_enabled_fast=true;
        g_config.processing_start=nr_processing::Start::Present;g_config.processing_selection=1;
        nr_layers::Migrate(g_config.nr_layers,layers,nr_layers::Primary(g_config));
        if(layers==2){g_config.nr_layers[0].intensity=1.1f;g_config.nr_layers[0].local_structure=.75f;}
        g_config.compat_percent=100;g_config.warmup_after_create=2;NormalizeConfig(g_config);g_config_generation=1;
        nr_processing_runtime::Reconcile();
        swprintf_s(g_log_path,L"%ls\\external-provider-%u.log",argv[1],layers);swprintf_s(g_ini_path,L"%ls\\host-%u.ini",argv[1],layers);
        // The external ABI intentionally calls SnapshotConfig/ReloadConfig.
        // Persist the fixture through the real writer, so missing INI defaults
        // cannot silently replace its in-memory two-layer request at Claim.
        Check(SaveConfig(g_config),"persist actual independent layer fixture");ReloadConfig(true);nr_processing_runtime::Reconcile();
        Check(nr_layers::Active(g_config.nr_layers)==layers,"actual INI reload preserves requested layer count");
        IDXGIFactory4* factory=nullptr;IDXGIAdapter1* adapter=nullptr;ID3D12Device* device=nullptr;ID3D12CommandQueue* queue=nullptr;
        Hr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory)),"factory");
        for(unsigned i=0;IDXGIFactory4_EnumAdapters1(factory,i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 desc{};IDXGIAdapter1_GetDesc1(adapter,&desc);
            if(desc.VendorId==0x10de&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){
                std::printf("NVIDIA name=%ls vendor=%x device=%x LUID=%08lx:%08lx\n",desc.Description,desc.VendorId,desc.DeviceId,
                    static_cast<unsigned long>(desc.AdapterLuid.HighPart),desc.AdapterLuid.LowPart);break;}ReleaseCom(adapter);}
        Check(adapter,"real NVIDIA adapter; no WARP fallback");
        Hr(D3D12CreateDevice(reinterpret_cast<IUnknown*>(adapter),D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void**>(&device)),"NVIDIA device");
        Check(GetModuleHandleW(L"_nvngx.dll")==nullptr,"cold loader, no host Init");
        Check(EnsureNrApi(device)&&EnsureCodec(device),"production cold NR/codec bootstrap");
        const auto cold=SnapshotRuntimeLoadEvidence();Check(cold.bootstrap_attempted&&cold.bootstrap_succeeded&&cold.bootstrap_result==1,"actual official cold bootstrap receipt");
        Metadata(device);D3D12_COMMAND_QUEUE_DESC q{};
        Hr(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,reinterpret_cast<void**>(&queue)),"producer queue");
        Io io;io.Init(device,queue);auto* depth=Guide(io,DXGI_FORMAT_R32_FLOAT);auto* motion=Guide(io,DXGI_FORMAT_R16G16_FLOAT);InitGuides(io,depth,motion);
        Check(InstallCommandStateObservation(io.list,true),"actual state observer hooks");
        auto* state=new NativeCommandState;(void)CaptureNativeCommandState(io.list,*state);ReleaseNativeCommandState(*state);delete state;
        NrExternalStatusV1 status{};Check(NRExternalProvider_ClaimOwnershipV1(kNrExternalProviderAbi,sizeof(status),provider,&status)==1&&status.owner_provider_id==provider,"actual ownership Claim");
        NrExternalFrameV1 frame{};frame.provider_id=provider;frame.view_id=1;frame.epoch=1;
        frame.lease_flags=static_cast<unsigned>(NrExternalLeaseFlags::CommandListOwnedByProducer)|static_cast<unsigned>(NrExternalLeaseFlags::QueueFenceRequired)|static_cast<unsigned>(NrExternalLeaseFlags::OutputSameAsColor);
        frame.color=Resource(io.backbuffer,D3D12_RESOURCE_STATE_PRESENT);frame.output=frame.color;
        frame.depth=Resource(depth,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);frame.motion=Resource(motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        frame.command_list=Wire(io.list);frame.queue=Wire(queue);frame.fence=Wire(io.fence);
        unsigned writes=0,completed=0;bool pending_checked=false,training_seen=false;
        for(unsigned attempt=1;attempt<=32&&writes<3;++attempt){
            io.Fill();RecordState(io);frame.frame_id=attempt;frame.fence_value=io.serial+1;
            const auto done_before=nr_external_provider_feedback::completion_frames.load();NrExternalSubmitResultV1 result{};
            const auto second_before=nr_dual_runtime::second_evaluations.load();
            const auto accepted=NRExternalProvider_SubmitFrameV1(kNrExternalProviderAbi,&frame,&result);
            Check(nr_layers::Active(g_config.nr_layers)==layers,"ABI snapshot retains independent layer request");
            std::printf("attempt=%u accepted=%d reason=%s writeback=%u pending=%u evaluations=%llu second=%llu\n",attempt,accepted,NrExternalProviderReasonName(result.reason),result.writeback_performed,
                nr_external_provider_feedback::pending_completions.load(),g_nr_successful_evaluations.load(),nr_dual_runtime::second_evaluations.load());
            if(accepted){
                Check(ID3D12Fence_GetCompletedValue(io.fence)<frame.fence_value,"real declared fence remains incomplete before Execute");
                Check(nr_external_provider_feedback::pending_completions.load()!=0,"recorded work retains actual fence receipt");
                Check(NRExternalProvider_ReleaseOwnershipV1(kNrExternalProviderAbi,provider)==0,"Release refuses real pending GPU work");
                Check(Query().owner_provider_id==provider&&nr_external_provider_feedback::completion_frames.load()==done_before,"Query cannot fake completion before Signal");
                NrExternalSubmitResultV1 duplicate{};
                Check(NRExternalProvider_SubmitFrameV1(kNrExternalProviderAbi,&frame,&duplicate)==0&&!duplicate.writeback_performed,"same external frame cannot record twice");
                auto* session=static_cast<nr_fg_runtime::Session*>(nullptr);
                for(auto& candidate:nr_fg_runtime::sessions)if(candidate.stream){session=&candidate;break;}
                Check(session&&session->images.raw&&session->primary,"actual pending consumer resources exist");
                auto* raw=session->images.raw;auto* primary=session->primary;
                EnterCriticalSection(&g_chain_cs);const bool releasable=CanDestroyContextLocked(*primary);LeaveCriticalSection(&g_chain_cs);
                Check(!releasable,"pending command list pins real model resources");
                nr_fg_runtime::CollectRetired();Check(session->images.raw==raw&&session->primary==primary,"collection cannot release pending model/images");
                pending_checked=true;
            }else{
                training_seen|=result.reason==NrExternalProviderReason::LeaseInvalid;
                Check(result.reason==NrExternalProviderReason::LeaseInvalid||result.reason==NrExternalProviderReason::RuntimeUnavailable,"only observed training/preparation refusal is retryable");
            }
            // No manual ProcessSubmitted call: execute the real installed queue hook.
            if(accepted&&completed==0)SubmitBlocked(io);else io.Submit();
            Check(ID3D12Fence_GetCompletedValue(io.fence)>=frame.fence_value,"producer exact Execute/Signal completed");
            Query();
            if(accepted){++completed;Check(nr_external_provider_feedback::completion_frames.load()==done_before+1&&nr_external_provider_feedback::pending_completions.load()==0,"Query consumes exactly the real completed fence");}
            const auto changed=io.Read();if(result.writeback_performed){Check(changed>0,"actual NR writeback changes synthetic colour pixels");
                if(layers==1||nr_dual_runtime::second_evaluations.load()>second_before)++writes;}
        }
        Check(training_seen&&pending_checked&&writes==3&&completed>=writes,"trained actual producer completes three changed outputs");
        unsigned evaluated=0;std::vector<NgxHandle*> handles;
        for(const auto& context:g_contexts)if(context.nr_handle&&context.evaluations){
            ++evaluated;handles.push_back(context.nr_handle);std::printf("actual layer=%u instance=%llu evaluations=%u\n",context.layer_index,context.instance_id,context.evaluations);}
        Check(evaluated==layers,"every requested independent Feature18 layer actually evaluated");
        if(layers==2)Check(handles[0]!=handles[1]&&nr_dual_runtime::second_evaluations.load()>0,"two distinct actual signed model instances");
        io.Fill();RecordState(io);++frame.frame_id;frame.fence_value=io.serial+1;
        g_config.enabled=false;g_enabled_fast=false;Check(SaveConfig(g_config),"persist actual OFF fixture");ReloadConfig(true);
        const auto before=nr_external_provider_feedback::completion_frames.load();
        const auto nr_before=g_nr_successful_evaluations.load();NrExternalSubmitResultV1 off{};
        Check(NRExternalProvider_SubmitFrameV1(kNrExternalProviderAbi,&frame,&off)==1&&off.reason==NrExternalProviderReason::Disabled&&!off.writeback_performed,"OFF returns explicit identity without work");
        io.Submit();Query();Check(nr_external_provider_feedback::completion_frames.load()==before&&g_nr_successful_evaluations.load()==nr_before&&io.Read()==0,"OFF submits no inference and preserves all pixels");
        Check(NRExternalProvider_ReleaseOwnershipV1(kNrExternalProviderAbi,provider)==1&&Query().owner_provider_id==0,"completed owner Release succeeds");
        nr_fg_runtime::CollectRetired();for(const auto& session:nr_fg_runtime::sessions)Check(!session.stream,"completed consumer transport retired");
        Check(SUCCEEDED(ID3D12Device_GetDeviceRemovedReason(device)),"no NVIDIA device removal");
        std::printf("PASS REAL_EXTERNAL_PROVIDER checks=%u layers=%u changed=%u completed=%u pending_release_refused=1 resource_retention=1 off=1; synthetic input; no game/native SR/FG/Feeder claim\n",checks,layers,writes,completed);
        ReleaseCom(depth);ReleaseCom(motion);io.Destroy();ReleaseCom(queue);ReleaseCom(device);ReleaseCom(adapter);ReleaseCom(factory);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL REAL_EXTERNAL_PROVIDER %s\n",error.what());return 1;}
}
