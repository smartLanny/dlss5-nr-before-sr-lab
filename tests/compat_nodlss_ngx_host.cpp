// No native SR/RR/FG feature is created. Real ReShade supplies every lifecycle
// callback; the complete production Core observes real output writes/submits.
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
int wmain(int argc,wchar_t** argv){using namespace host_ngx;using namespace nodlss_test;namespace compat=nr_compat_present_runtime;
    std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Crash);
    try {
        Check(argc==3,"usage no-DLSS-host runtime-directory passes");const unsigned passes=std::wcstoul(argv[2],nullptr,10);Check(passes==1||passes==4,"bounded NR case");
        InitializeCriticalSection(&g_chain_cs);g_config=ChainConfig{};g_config.enabled=true;g_enabled_fast=true;
        g_config.processing_start=nr_processing::Start::Before;g_config.processing_selection=1;g_config.transfer_strength=1;g_config.compat_percent=100;
        g_config.nr_passes=passes==1?1:2;g_config.nr_full_reference=passes==4;g_config.nr_full_passes=passes==4?4:2;g_config.warmup_after_create=2;
        nr_processing_runtime::Reconcile();g_config_generation=1;
        swprintf_s(g_log_path,L"%ls\\no-dlss-core.log",argv[1]);swprintf_s(g_ini_path,L"%ls\\no-dlss-host.ini",argv[1]);
        Check(GetProcAddress(GetModuleHandleW(L"dxgi.dll"),"ReShadeRegisterAddon")!=nullptr,"actual ReShade DXGI proxy loaded");Register();
        WNDCLASSW wc{};wc.lpfnWndProc=Window;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NR_No_DLSS_Production_Activation";
        Check(RegisterClassW(&wc)!=0,"hidden window class");auto window=CreateWindowW(wc.lpszClassName,L"Controlled no-DLSS NR host",WS_OVERLAPPEDWINDOW,0,0,Io::width,Io::height,nullptr,nullptr,wc.hInstance,nullptr);
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
        unsigned changed_frames=0;bool verified_duplicate=false;unsigned long long last_adopted_source=0;
        for(unsigned frame=0;frame<48&&changed_frames<4;++frame){Pump();const unsigned index=IDXGISwapChain3_GetCurrentBackBufferIndex(swap);
            io.backbuffer=buffers[index];const float color[4]={.25f,.5f,.75f,1};std::fill(io.reference.begin(),io.reference.end(),0xffbf8040u);
            io.Reset();auto state=D3D12_RESOURCE_STATE_PRESENT;Transition(io.list,io.backbuffer,state,D3D12_RESOURCE_STATE_RENDER_TARGET);
            auto rtv=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtvs);rtv.ptr+=index*stride;
            ID3D12GraphicsCommandList_OMSetRenderTargets(io.list,1,&rtv,FALSE,nullptr);
            ID3D12GraphicsCommandList_ClearRenderTargetView(io.list,rtv,color,0,nullptr);
            Transition(io.list,io.backbuffer,state,D3D12_RESOURCE_STATE_PRESENT);io.Submit();
            io.Read();io.reference=io.pixels; // Actual clear bytes, not assumed UNORM rounding.
            if(frame<5)Receipt("after-application-submit",io.backbuffer);
            capture_io=&io;
            const auto before=compat::observation.submitted;Hr(IDXGISwapChain3_Present(swap,0,0),"actual native Present delivers Core compatibility callback");
            if(compat::current&&compat::current->timeline.Last())Wait(compat::current->fence,compat::current->timeline.Last());
            Check(capture_io==nullptr,"actual post-Core pre-native-Present output callback completed");
            const unsigned changed=captured_changed;const auto after=compat::observation.submitted;
            if(frame<5)Receipt("after-present-and-readback",io.backbuffer);
            std::printf("source=%u callbacks device/list/reset/execute/present=%u/%u/%u/%u/%u submitted=%llu status=%s actualRoute=%s pixelsChanged=%u carriers=%llu\n",frame,devices,lists,resets,executes,presents,after,nr_compat_present::Name(compat::observation.status),nr_processing::Name(nr_processing_runtime::Effective()),changed,g_carrier_evaluations.load());
            Check(g_carrier_evaluations.load()==0,"no native SR RR FG carrier used to activate source observation");
            if(after>before&&changed){++changed_frames;
                Check(compat::observation.submitted_source>last_adopted_source,"each newly adopted clear has a fresh observed content receipt");last_adopted_source=compat::observation.submitted_source;
                if(!verified_duplicate){const auto serial=compat::observation.submitted;
                    // Two real flips return to this exact already processed
                    // resource without any new application output write.
                    Hr(IDXGISwapChain3_Present(swap,0,0),"duplicate Present first flip");Hr(IDXGISwapChain3_Present(swap,0,0),"duplicate Present same source resource");
                    Check(compat::observation.submitted==serial&&compat::observation.submitted_source==last_adopted_source,"same source duplicate Presents record no new NR");verified_duplicate=true;}}
        }
        Check(changed_frames==4&&verified_duplicate,"no-DLSS actual source path produced four adopted NR outputs");
        Check(nr_processing_runtime::Effective()==nr_processing::Start::Present,"default before route automatically reached compatibility");
        Check(devices&&lists&&resets&&executes&&presents,"all actual ReShade lifecycle callbacks occurred");
        Check(g_nr_api.initialized&&GetModuleHandleW(L"_nvngx.dll")!=nullptr,"production OnPresent performed the official cold SDK initialization");
        for(unsigned i=0;i<passes;++i)Check(compat::current&&compat::current->layers[i].context.nr_handle&&compat::current->layers[i].context.evaluations>=4,"every requested real NR layer continued evaluating");
        FILE* image=nullptr;const auto output=std::wstring(argv[1])+L"\\no-dlss-p"+std::to_wstring(passes)+L".rgba";
        _wfopen_s(&image,output.c_str(),L"wb");Check(image&&std::fwrite(io.pixels.data(),4,io.pixels.size(),image)==io.pixels.size(),"actual no-DLSS GPU output saved");std::fclose(image);
        const auto submitted=compat::observation.submitted;g_config.enabled=false;g_enabled_fast=false;nr_processing_runtime::Reconcile();
        for(unsigned i=0;i<4;++i){const auto index=IDXGISwapChain3_GetCurrentBackBufferIndex(swap);io.backbuffer=buffers[index];io.Reset();auto state=D3D12_RESOURCE_STATE_PRESENT;
            Transition(io.list,io.backbuffer,state,D3D12_RESOURCE_STATE_RENDER_TARGET);auto rtv=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtvs);rtv.ptr+=index*stride;
            const float off_color[4]={.5f,.25f,.125f,1};ID3D12GraphicsCommandList_ClearRenderTargetView(io.list,rtv,off_color,0,nullptr);Transition(io.list,io.backbuffer,state,D3D12_RESOURCE_STATE_PRESENT);io.Submit();
            Hr(IDXGISwapChain3_Present(swap,0,0),"disabled real fresh-source Present");if(compat::current&&compat::current->timeline.Last())Wait(compat::current->fence,compat::current->timeline.Last());}
        Check(compat::observation.submitted==submitted&&!compat::current,"master off starts no NR and retires actual completed private context");
        // Exercise actual view destruction/recreation while the master is off.
        io.backbuffer=nullptr;for(auto& buffer:buffers)ReleaseCom(buffer);
        Hr(IDXGISwapChain3_ResizeBuffers(swap,2,Io::width,Io::height,DXGI_FORMAT_R8G8B8A8_UNORM,0),"real disabled swapchain resize");
        for(unsigned i=0;i<2;++i){Hr(IDXGISwapChain3_GetBuffer(swap,i,IID_ID3D12Resource,reinterpret_cast<void**>(&buffers[i])),"real resized backbuffer");
            auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtvs);cpu.ptr+=i*stride;ID3D12Device_CreateRenderTargetView(device,buffers[i],nullptr,cpu);}
        Check(compat::observation.submitted==submitted&&!compat::current,"disabled resize and new views start no NR");
        g_config.enabled=true;g_enabled_fast=true;nr_processing_runtime::Reconcile();
        unsigned resumed=0;
        for(unsigned frame=0;frame<48&&resumed<4;++frame){const unsigned index=IDXGISwapChain3_GetCurrentBackBufferIndex(swap);io.backbuffer=buffers[index];
            io.Reset();auto state=D3D12_RESOURCE_STATE_PRESENT;Transition(io.list,io.backbuffer,state,D3D12_RESOURCE_STATE_RENDER_TARGET);
            auto rtv=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtvs);rtv.ptr+=index*stride;const float color[4]={.25f,.5f,.75f,1};
            ID3D12GraphicsCommandList_OMSetRenderTargets(io.list,1,&rtv,FALSE,nullptr);ID3D12GraphicsCommandList_ClearRenderTargetView(io.list,rtv,color,0,nullptr);
            Transition(io.list,io.backbuffer,state,D3D12_RESOURCE_STATE_PRESENT);io.Submit();io.Read();io.reference=io.pixels;
            const auto before=compat::observation.submitted;capture_io=&io;
            Hr(IDXGISwapChain3_Present(swap,0,0),"enabled Present after disabled resize/view creation");
            Check(capture_io==nullptr&&g_carrier_evaluations.load()==0,"resized real callbacks retain no-DLSS activation");
            if(compat::observation.submitted>before){Check(captured_changed>0,"resized newly adopted NR changes actual clear bytes");++resumed;}
        }
        Check(resumed==4,"views created while off resume sustained real NR after enabling");
        const auto resumed_submitted=compat::observation.submitted;g_config.enabled=false;g_enabled_fast=false;nr_processing_runtime::Reconcile();
        for(unsigned i=0;i<3;++i)Hr(IDXGISwapChain3_Present(swap,0,0),"final disabled retirement Present");
        Check(!compat::current&&compat::observation.submitted==resumed_submitted,"final disabled state retires resized NR without new work");
        std::printf("PASS NO_DLSS_ACTIVATION checks=%u passes=%u adopted=%u carriers=0 realReShade=1 realNGX=1 duplicateSkipped=1 disabledStopped=1 offResizeResumed=%u; controlled clear output, no game compatibility claim\n",checks,passes,changed_frames,resumed);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL NO_DLSS_ACTIVATION %s\n",e.what());return 1;}
}
