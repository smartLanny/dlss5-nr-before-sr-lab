// Controlled native-seam metadata; real owned guide copies, ReShade callbacks,
// queue fences, production OnPresent and signed NVIDIA NR. No real SR/game claim.
#define wmain unused_present_ngx_entry
#include "compat_present_ngx_host.cpp"
#undef wmain
#include <dbghelp.h>
namespace nodlss_test {
using namespace host_ngx;
reshade::api::device* api_device=nullptr;reshade::api::command_queue* api_queue=nullptr;reshade::api::swapchain* api_swapchain=nullptr;
unsigned devices=0,lists=0,resets=0,executes=0,presents=0;
Io* capture_io=nullptr;unsigned captured_changed=0;
LONG WINAPI Crash(EXCEPTION_POINTERS* exception){
    HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(exception->ExceptionRecord->ExceptionAddress),&module);
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(module,path,MAX_PATH);
    std::printf("NO_DLSS_FAULT code=0x%08lX pc=%p moduleBase=%p module=%ls\n",exception->ExceptionRecord->ExceptionCode,exception->ExceptionRecord->ExceptionAddress,module,path);
    HANDLE file=CreateFileW(L"no-dlss-fault.dmp",GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),exception,FALSE};
        MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpWithThreadInfo,&info,nullptr,nullptr);CloseHandle(file);}
    return EXCEPTION_EXECUTE_HANDLER;
}
void Device(reshade::api::device* value){api_device=value;++devices;}
void List(reshade::api::command_list*){++lists;}
void Reset(reshade::api::command_list*){++resets;}
void Execute(reshade::api::command_queue* queue,reshade::api::command_list*){api_queue=queue;++executes;}
void Surface(reshade::api::swapchain* chain,bool){api_swapchain=chain;}
void View(reshade::api::device*,reshade::api::resource resource,reshade::api::resource_usage usage,const reshade::api::resource_view_desc& desc,reshade::api::resource_view view){
    if(usage==reshade::api::resource_usage::render_target)std::printf("actual RTV init resource=%llx handle=%llx type=%u level=%u layer=%u\n",resource.handle,view.handle,unsigned(desc.type),desc.texture.first_level,desc.texture.first_layer);
}
void Present(reshade::api::command_queue* queue,reshade::api::swapchain* chain,const reshade::api::rect*,const reshade::api::rect*,std::uint32_t,const reshade::api::rect*){
    api_queue=queue;api_swapchain=chain;++presents;
    // Registered after the real production callback. Read its completed output
    // before native flip-discard Present makes the old buffer undefined.
    if(capture_io){auto* io=capture_io;capture_io=nullptr;
        if(nr_compat_present_runtime::current&&nr_compat_present_runtime::current->timeline.Last())
            Wait(nr_compat_present_runtime::current->fence,nr_compat_present_runtime::current->timeline.Last());
        captured_changed=io->Read();}
}
LRESULT CALLBACK Window(HWND window,UINT message,WPARAM w,LPARAM l){return DefWindowProcW(window,message,w,l);}
void Register(){
    Check(reshade::register_addon(GetModuleHandleW(nullptr)),"actual ReShade addon API registration");
    reshade::register_event<reshade::addon_event::init_device>(Device);
    reshade::register_event<reshade::addon_event::init_device>(OnCommandStateInitDevice);
    reshade::register_event<reshade::addon_event::init_device>(OnNrFormatInitDevice);
    reshade::register_event<reshade::addon_event::init_command_list>(OnCommandStateInitList);
    reshade::register_event<reshade::addon_event::init_command_list>(List);
    reshade::register_event<reshade::addon_event::reset_command_list>(OnResetCommandList);
    reshade::register_event<reshade::addon_event::reset_command_list>(Reset);
    reshade::register_event<reshade::addon_event::execute_command_list>(OnExecuteCommandList);
    reshade::register_event<reshade::addon_event::execute_command_list>(Execute);
    reshade::register_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);
    reshade::register_event<reshade::addon_event::destroy_command_queue>(OnDestroyCommandQueue);
    RegisterPostBoundaryEvents(true);
    nr_compat_present_runtime::Register(true); // Registers the production source startup callbacks too.
    reshade::register_event<reshade::addon_event::init_swapchain>(Surface);
    reshade::register_event<reshade::addon_event::init_resource_view>(View);
    reshade::register_event<reshade::addon_event::present>(Present);
}
void Pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
void Receipt(const char* phase,ID3D12Resource* resource){
    nr_source_runtime::Lock lock;const auto identity=nr_source_runtime::ResourceOf(resource);const auto label=nr_source_runtime::lineage.Lookup(identity);
    std::printf("receipt phase=%s resource=%llx lifetime=%llu device=%llu source=%llu/%llu/%llu role=%u content=%llu queue=%llu submitted=%u exact=%u processed=%u tainted=%u fg=%u\n",phase,identity.address,identity.lifetime,identity.device,label.source.stream,label.source.epoch,label.source.sequence,unsigned(label.role),label.content,label.queue,label.submitted,label.exact_write,label.processed,label.tainted,label.fg_consumed);
    for(const auto& view:nr_source_runtime::source_rtvs)if(view.resource.address==identity.address)std::printf("receipt RTV handle=%llx whole=%u lifetime=%llu device=%llu\n",view.handle,view.whole,view.resource.lifetime,view.resource.device);
}
}

namespace late_host {
using namespace host_ngx;
bool SameComObject(IUnknown* first,IUnknown* second) {
    if(first==second)return true;
    IUnknown *first_identity=nullptr,*second_identity=nullptr;
    const bool first_ok=ResolveRrDepthDeviceIdentity(first,&first_identity);
    const bool second_ok=ResolveRrDepthDeviceIdentity(second,&second_identity);
    const bool same=first_ok&&second_ok&&first_identity==second_identity;
    ReleaseCom(first_identity);ReleaseCom(second_identity);return same;
}
struct Guides {
    ID3D12Resource *depth=nullptr,*motion=nullptr,*uploads[2]{};
    NrGuideBinding binding{};NgxParams* params=nullptr;FeatureRecord feature{};
    void Init(Io& io) {
        io.Reset();
        for(unsigned i=0;i<2;++i){
            D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width=Io::width;desc.Height=Io::height;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
            desc.Format=i?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R32_FLOAT;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            auto** image=i?&motion:&depth;
            Hr(ID3D12Device_CreateCommittedResource(io.device,&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(image)),"fixture guide allocation");
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;ID3D12Device_GetCopyableFootprints(io.device,&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
            auto bd=desc;bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;bd.Format=DXGI_FORMAT_UNKNOWN;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            heap.Type=D3D12_HEAP_TYPE_UPLOAD;
            Hr(ID3D12Device_CreateCommittedResource(io.device,&heap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&uploads[i])),"fixture guide upload");
            void* data=nullptr;D3D12_RANGE none{};Hr(ID3D12Resource_Map(uploads[i],0,&none,&data),"fixture guide map");std::memset(data,0,bytes);
            if(!i)for(unsigned y=0;y<Io::height;++y)for(unsigned x=0;x<Io::width;++x){float d=.5f;std::memcpy(static_cast<char*>(data)+fp.Offset+y*fp.Footprint.RowPitch+x*4,&d,4);}
            ID3D12Resource_Unmap(uploads[i],0,nullptr);
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=uploads[i];from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
            to.pResource=*image;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;ID3D12GraphicsCommandList_CopyTextureRegion(io.list,&to,0,0,0,&from,nullptr);
            auto state=D3D12_RESOURCE_STATE_COPY_DEST;Transition(io.list,*image,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        io.Submit();
        binding.depth=depth;binding.motion=motion;binding.has_motion_scale_x=binding.has_motion_scale_y=binding.valid_motion_scale_x=binding.valid_motion_scale_y=true;
        auto& s=binding.signature;s.depth_physical_width=s.motion_physical_width=s.color_physical_width=s.depth_width=s.motion_source_width=s.motion_bound_width=Io::width;
        s.depth_physical_height=s.motion_physical_height=s.color_physical_height=s.depth_height=s.motion_source_height=s.motion_bound_height=Io::height;
        s.depth_format=DXGI_FORMAT_R32_FLOAT;s.motion_format=DXGI_FORMAT_R16G16_FLOAT;s.color_format=DXGI_FORMAT_R8G8B8A8_UNORM;s.motion_scale_x=s.motion_scale_y=1;
        Check(g_nr_api.allocate(&params)==kNgxSuccess&&params,"actual NGX parameter object for capture entry");
        feature.feature_id=1;feature.generation=1;feature.create_flags=0;feature.width=Io::width;feature.height=Io::height;feature.out_width=Io::width;feature.out_height=Io::height;
        SetResource(params,"Color",io.backbuffer);SetResource(params,"Output",io.backbuffer);SetResource(params,"Depth",depth);SetResource(params,"MotionVectors",motion);
        SetUInt(params,"Width",Io::width);SetUInt(params,"Height",Io::height);SetUInt(params,"OutWidth",Io::width);SetUInt(params,"OutHeight",Io::height);
        SetUInt(params,"DLSS.Render.Subrect.Dimensions.Width",Io::width);SetUInt(params,"DLSS.Render.Subrect.Dimensions.Height",Io::height);
        SetFloat(params,"MV.Scale.X",1);SetFloat(params,"MV.Scale.Y",1);SetFloat(params,"Jitter.Offset.X",0);SetFloat(params,"Jitter.Offset.Y",0);
    }
    void Produce(Io& io,unsigned frame) {
        namespace S=nr_source_runtime;
        io.Reset();NrPostBoundaryAliasScope alias(io.list);
        S::Source source{};
        {S::Lock lock;S::ObserveNativeProducer(0xD170,1,S::DeviceOf(reinterpret_cast<ID3D12DeviceChild*>(io.list)));source=S::ProducerSource(0xD170,1);}
        S::BeginNativeReceiptOpportunity(source);
        for(auto* image:{depth,motion}){auto state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;Transition(io.list,image,state,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(io.list,image,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
        binding.game_reset=frame==0?1:0;
        SetUInt(params,"Reset",frame==0?1:0);
        // Real inputs remain NON_PIXEL_SHADER_RESOURCE; only observer evidence is absent.
        {S::Lock lock;for(auto& state:S::resource_states)if(state.resource==S::ResourceOf(depth)||state.resource==S::ResourceOf(motion))state={};}
        ID3D12GraphicsCommandList_SetPipelineState(io.list,g_codec.encode);ID3D12GraphicsCommandList_SetComputeRootSignature(io.list,g_codec.root);
        void* predicate_data=nullptr;D3D12_RANGE predicate_read{0,sizeof(UINT64)};Hr(ID3D12Resource_Map(io.upload,0,&predicate_read,&predicate_data),"map predicate input");
        UINT64 predicate_value=0;std::memcpy(&predicate_value,predicate_data,sizeof(predicate_value));D3D12_RANGE predicate_written{};ID3D12Resource_Unmap(io.upload,0,&predicate_written);
        Check(predicate_value!=0,"nonzero EQUAL_ZERO predicate blocks an unguarded private dispatch");
        ID3D12GraphicsCommandList_SetPredication(io.list,io.upload,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
        S::CaptureNgxGuides(io.list,params,feature,source);
        S::GuideReceiptDiagnostics d{};Check(S::TryGuideReceiptDiagnostics(d),"capture entry diagnostic published");
        std::printf("capture stage=%s shader=%d\n",d.last.stage,d.last.shader_snapshot);
        if(std::strcmp(d.last.stage,"guide-command-state-unavailable")==0&&frame<4){
            S::CompleteNativeReceipt(source,true);io.Submit();return; // fresh hook publication needs the next real Reset
        }
        Check(std::strcmp(d.last.stage,"recorded")==0&&d.last.shader_snapshot,"real CaptureNgxGuides binding and shader transport recorded");
        NativeCommandState restored{};Check(CaptureNativeCommandState(io.list,restored),"capture native restored state");
        Check(restored.pipeline==g_codec.encode&&restored.roots[0]==g_codec.root,"native PSO and compute root restored after guide snapshot");
        const bool raw_predicate=restored.predicate==io.upload;
        const bool same_predicate=SameComObject(reinterpret_cast<IUnknown*>(restored.predicate),reinterpret_cast<IUnknown*>(io.upload));
        std::printf("predicate rawSame=%u identitySame=%u recorded=%u offset=%llu operation=%u\n",raw_predicate,same_predicate,
            !!(restored.dynamic.recorded&kDynamicPredicate),static_cast<unsigned long long>(restored.dynamic.predicate_offset),unsigned(restored.dynamic.predicate_operation));
        Check(same_predicate&&(restored.dynamic.recorded&kDynamicPredicate)&&restored.dynamic.predicate_offset==0&&
            restored.dynamic.predicate_operation==D3D12_PREDICATION_OP_EQUAL_ZERO,"native predicate identity and parameters restored after private snapshot");ReleaseNativeCommandState(restored);
        ID3D12GraphicsCommandList_SetPredication(io.list,nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
        S::CompleteNativeReceipt(source,true);
        io.Submit();
    }
};
}
int wmain(int argc,wchar_t** argv){using namespace host_ngx;using namespace nodlss_test;namespace compat=nr_compat_present_runtime;
    std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Crash);
    try {
        Check(argc==4,"usage late-present runtime-directory passes cross-queue");const unsigned passes=std::wcstoul(argv[2],nullptr,10);Check(passes==1||passes==4,"bounded layer case");
        const bool cross=std::wcstoul(argv[3],nullptr,10)!=0;
        InitializeCriticalSection(&g_chain_cs);g_config=ChainConfig{};g_config.enabled=true;g_enabled_fast=true;
        g_config.processing_start=nr_processing::Start::Present;g_config.processing_selection=1;g_config.transfer_strength=1;g_config.compat_percent=100;
        g_config.nr_passes=passes==1?1:2;g_config.nr_full_reference=passes==4;g_config.nr_full_passes=passes==4?4:2;g_config.warmup_after_create=2;
        nr_processing_runtime::Reconcile();g_config_generation=1;
        swprintf_s(g_log_path,L"%ls\\late-present-core.log",argv[1]);swprintf_s(g_ini_path,L"%ls\\no-dlss-host.ini",argv[1]);
        Check(GetProcAddress(GetModuleHandleW(L"dxgi.dll"),"ReShadeRegisterAddon")!=nullptr,"actual ReShade DXGI proxy loaded");Register();
        WNDCLASSW wc{};wc.lpfnWndProc=Window;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NR_Late_Present_Receipt_Test";
        Check(RegisterClassW(&wc)!=0,"hidden window class");auto window=CreateWindowW(wc.lpszClassName,L"Controlled late Present receipt host",WS_OVERLAPPEDWINDOW,0,0,Io::width,Io::height,nullptr,nullptr,wc.hInstance,nullptr);
        Check(window!=nullptr&&!IsWindowVisible(window),"real hidden host window");
        IDXGIFactory4* factory=nullptr;IDXGIAdapter1* adapter=nullptr;ID3D12Device* device=nullptr;ID3D12CommandQueue* queue=nullptr;
        Hr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory)),"actual ReShade DXGI factory");
        for(unsigned i=0;IDXGIFactory4_EnumAdapters1(factory,i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};IDXGIAdapter1_GetDesc1(adapter,&d);
            if(d.VendorId==0x10de&&!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){std::printf("actual GPU vendor=%x device=%x\n",d.VendorId,d.DeviceId);break;}ReleaseCom(adapter);}
        Check(adapter!=nullptr,"actual NVIDIA adapter");Hr(D3D12CreateDevice(reinterpret_cast<IUnknown*>(adapter),D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void**>(&device)),"actual ReShade D3D12 device");
        Check(devices>0&&api_device&&api_device->get_api()==reshade::api::device_api::d3d12,"actual init_device callback delivered");
        Check(GetModuleHandleW(L"_nvngx.dll")==nullptr&&!g_nr_api.initialized,"cold startup has no host or game SDK initialization");
        D3D12_COMMAND_QUEUE_DESC q{};Hr(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,reinterpret_cast<void**>(&queue)),"actual ReShade graphics queue");
        DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=Io::width;sd.Height=Io::height;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        IDXGISwapChain1* initial=nullptr;IDXGISwapChain3* swap=nullptr;
        Hr(IDXGIFactory4_CreateSwapChainForHwnd(factory,reinterpret_cast<IUnknown*>(queue),window,&sd,nullptr,nullptr,&initial),"actual ReShade swapchain");
        Hr(IDXGISwapChain1_QueryInterface(initial,IID_IDXGISwapChain3,reinterpret_cast<void**>(&swap)),"actual swapchain3");
        Check(api_swapchain!=nullptr,"actual init_swapchain callback delivered");
        ID3D12Resource* buffers[2]{};ID3D12DescriptorHeap* rtvs=nullptr;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=2;
        Hr(ID3D12Device_CreateDescriptorHeap(device,&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(&rtvs)),"real RTV descriptors");const auto stride=ID3D12Device_GetDescriptorHandleIncrementSize(device,hd.Type);
        for(unsigned i=0;i<2;++i){Hr(IDXGISwapChain3_GetBuffer(swap,i,IID_ID3D12Resource,reinterpret_cast<void**>(&buffers[i])),"actual swapchain backbuffer");auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtvs);cpu.ptr+=i*stride;ID3D12Device_CreateRenderTargetView(device,buffers[i],nullptr,cpu);}
        Io io;io.Init(device,queue);ReleaseCom(io.backbuffer);io.backbuffer=nullptr;
        Check(lists>0&&g_carrier_evaluations.load()==0,"actual init list callbacks with no reconstruction carrier");

        // No receipt: current real backbuffer must stay untouched, with no NR context.
        for(unsigned n=0;n<3;++n){io.backbuffer=buffers[IDXGISwapChain3_GetCurrentBackBufferIndex(swap)];io.Fill();capture_io=&io;Hr(IDXGISwapChain3_Present(swap,0,0),"no-guide Present");Check(!compat::current&&compat::observation.submitted==0&&captured_changed==0,"no native guides means no NR and identity output");}
        ID3D12CommandQueue* producer_queue=queue;ID3D12CommandQueue_AddRef(producer_queue);
        if(cross){ReleaseCom(producer_queue);Hr(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,reinterpret_cast<void**>(&producer_queue)),"second real producer queue");}
        Check(EnsureNrApi(device)&&EnsureCodec(device),"native runtime and codec for true capture entry");
        Io producer;producer.Init(device,producer_queue);late_host::Guides guides;guides.Init(producer);
        unsigned changed_frames=0;bool duplicate_checked=false;unsigned long long previous_source=0;
        for(unsigned frame=0;frame<48&&changed_frames<4;++frame){
            Pump();io.backbuffer=buffers[IDXGISwapChain3_GetCurrentBackBufferIndex(swap)];io.Fill();guides.Produce(producer,frame);
            // Deliberately make lineage unknown without altering the guide receipt.
            {nr_source_runtime::Lock lock;nr_source_runtime::lineage.InvalidateDevice(nr_source_runtime::RecordingOf(producer.list));}
            capture_io=&io;auto before=compat::observation.submitted;
            Hr(IDXGISwapChain3_Present(swap,0,0),"receipt-gated real Present");
            auto after=compat::observation.submitted;
            nr_source_frame::PresentObservation receipt{};Check(nr_source_runtime::TryPresentObservation(receipt),"Present diagnostic available");
            std::printf("frame=%u cross=%d status=%s accepted=%d submitted=%llu changed=%u\n",frame,cross,nr_compat_present::Name(compat::observation.status),receipt.accepted,after,captured_changed);
            if(after>before){Check(receipt.receipt_cross_queue_wait==cross,"diagnostic reports actual producer/Present queue relation");for(unsigned i=0;i<io.pixels.size();++i)if((io.pixels[i]>>24)!=(io.reference[i]>>24))throw std::runtime_error("Present changed source alpha");Check(true,"actual output alpha preserved");Check(receipt.lookup.role==nr_source_frame::Role::Unknown,"unknown lineage is diagnostic only");Check(captured_changed>0,"production NR changed actual current backbuffer");Check(compat::observation.submitted_source>previous_source,"new receipt for each adopted output");previous_source=compat::observation.submitted_source;++changed_frames;
                if(!duplicate_checked){auto count=after;Hr(IDXGISwapChain3_Present(swap,0,0),"duplicate flip one");Hr(IDXGISwapChain3_Present(swap,0,0),"duplicate flip two");Check(compat::observation.submitted==count,"consumed receipt never runs NR on duplicate Present");duplicate_checked=true;}
            }
        }
        Check(changed_frames==4&&duplicate_checked,"four sustained receipt-gated real NR outputs");
        Check(compat::current&&compat::current->uses_native_guides,"native owned guides selected without synthetic fallback");
        auto submitted=compat::observation.submitted;g_config.enabled=false;g_enabled_fast=false;nr_processing_runtime::Reconcile();
        for(unsigned i=0;i<3;++i)Hr(IDXGISwapChain3_Present(swap,0,0),"disabled receipt Present");
        Check(compat::observation.submitted==submitted&&!compat::current,"F6 off stops NR and retires completed context");
        auto source=nr_source_runtime::Source{1,1,999};Check(!nr_source_runtime::CaptureGuides(producer.list,source,guides.binding,Io::width,Io::height),"F6 off adds no owned guide copy");
        std::printf("PASS LATE_PRESENT_RECEIPT checks=%u layers=%u crossQueue=%d adopted=%u realReShade=1 realNR=1 nativeSeam=controlled; not real SR or game verification\n",checks,passes,cross,changed_frames);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL LATE_PRESENT_RECEIPT %s\n",e.what());return 1;}
}
