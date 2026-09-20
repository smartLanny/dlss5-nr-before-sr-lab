// Real signed Feature18 Create/Bind/Release on a frozen Core. Artificial
// textures match selected shape/format/flags, not the game's complete inputs.
#define wmain present_reference_wmain
#include "compat_present_ngx_host.cpp"
#undef wmain
#include <psapi.h>

namespace scale_host {
using namespace host_ngx;
IDXGIAdapter3* memory_adapter=nullptr;
void Memory(const char* phase,unsigned w=0,unsigned h=0) {
    MEMORYSTATUSEX global{};global.dwLength=sizeof(global);
    PROCESS_MEMORY_COUNTERS_EX process{};process.cb=sizeof(process);
    PERFORMANCE_INFORMATION perf{};perf.cb=sizeof(perf);
    DXGI_QUERY_VIDEO_MEMORY_INFO local{},nonlocal{};
    const bool gm=GlobalMemoryStatusEx(&global)!=FALSE;
    const bool pm=GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process),sizeof(process))!=FALSE;
    const bool pi=GetPerformanceInfo(&perf,sizeof(perf))!=FALSE;
    const HRESULT lm=memory_adapter?IDXGIAdapter3_QueryVideoMemoryInfo(memory_adapter,0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local):E_FAIL;
    const HRESULT nm=memory_adapter?IDXGIAdapter3_QueryVideoMemoryInfo(memory_adapter,0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&nonlocal):E_FAIL;
    std::printf("MEM phase=%s extent=%ux%u globalValid=%d physAvail=%llu physTotal=%llu pageAvail=%llu pageTotal=%llu processValid=%d private=%llu workingSet=%llu localHr=%08lX usage=%llu budget=%llu nonlocalHr=%08lX nonlocalUsage=%llu nonlocalBudget=%llu\n",
        phase,w,h,gm,global.ullAvailPhys,global.ullTotalPhys,global.ullAvailPageFile,global.ullTotalPageFile,pm,
        static_cast<unsigned long long>(process.PrivateUsage),static_cast<unsigned long long>(process.WorkingSetSize),
        static_cast<unsigned long>(lm),local.CurrentUsage,local.Budget,static_cast<unsigned long>(nm),nonlocal.CurrentUsage,nonlocal.Budget);
    std::printf("COMMIT phase=%s valid=%d systemTotal=%llu systemLimit=%llu systemAvailable=%llu systemPeak=%llu processPeakCommit=%llu\n",
        phase,pi,static_cast<unsigned long long>(perf.CommitTotal)*perf.PageSize,
        static_cast<unsigned long long>(perf.CommitLimit)*perf.PageSize,
        static_cast<unsigned long long>(perf.CommitLimit-perf.CommitTotal)*perf.PageSize,
        static_cast<unsigned long long>(perf.CommitPeak)*perf.PageSize,
        static_cast<unsigned long long>(process.PeakPagefileUsage));
    std::fflush(stdout);std::fflush(stderr);
}
LONG WINAPI Fault(EXCEPTION_POINTERS* p){
    const auto* e=p->ExceptionRecord;
    std::fprintf(stderr,"UNHANDLED code=0x%08lX address=%p operation=%llu addressAccessed=0x%llX\n",
        e->ExceptionCode,e->ExceptionAddress,e->NumberParameters?static_cast<unsigned long long>(e->ExceptionInformation[0]):0,
        e->NumberParameters>1?static_cast<unsigned long long>(e->ExceptionInformation[1]):0);
    std::fflush(stderr);return EXCEPTION_EXECUTE_HANDLER;
}
// Initialize the artificial textures by an ordinary upload, completed before
// the model call. No application descriptor/native-source evidence is forged.
ID3D12Resource* Texture(Io& io,unsigned w,unsigned h,DXGI_FORMAT format,bool output) {
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
    d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    ID3D12Resource* resource=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(io.device,&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
        IID_ID3D12Resource,reinterpret_cast<void**>(&resource)),"test texture allocation");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 size=0;
    ID3D12Device_GetCopyableFootprints(io.device,&d,0,1,0,&footprint,nullptr,nullptr,&size);
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=size;
    buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type=D3D12_HEAP_TYPE_UPLOAD;ID3D12Resource* upload=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(io.device,&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
        IID_ID3D12Resource,reinterpret_cast<void**>(&upload)),"test upload allocation");
    void* mapped=nullptr;D3D12_RANGE none{0,0};Hr(ID3D12Resource_Map(upload,0,&none,&mapped),"test upload map");
    std::memset(mapped,0,static_cast<size_t>(size));
    if(format==DXGI_FORMAT_R32_FLOAT)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const float one=1.f;std::memcpy(static_cast<char*>(mapped)+y*footprint.Footprint.RowPitch+4*x,&one,4);}
    ID3D12Resource_Unmap(upload,0,nullptr);
    io.Reset();D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
    to.pResource=resource;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(io.list,&to,0,0,0,&from,nullptr);
    auto state=D3D12_RESOURCE_STATE_COPY_DEST;Transition(io.list,resource,state,output?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    io.Submit();ReleaseCom(upload);return resource;
}
struct Model {NrContext* context=new NrContext{};ID3D12Resource *depth=nullptr,*motion=nullptr;unsigned w=0,h=0;bool safe=false;};
bool Create(Io& io,Model& m,unsigned w,unsigned h) {
    m.w=w;m.h=h;auto& c=*m.context;auto& slot=c.slots[0];
    c.width=w;c.height=h;c.mode=1;c.tuning_signature=CurrentNrTuningSignature();
    c.tuning_signature.intensity=1.2f;c.tuning_signature.skin_structure=-1.f;c.tuning_signature.auto_mask=false;c.tuning_signature.ui_correction=true;
    c.tuning_signature_initialized=true;c.bound_create_flags=73;c.bound_perf_quality=5;
    Memory("before-resources",w,h);
    slot.proxy=Texture(io,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,false);
    slot.neural=Texture(io,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    m.depth=Texture(io,2228,1253,DXGI_FORMAT_R32_FLOAT,false);
    m.motion=Texture(io,w,h,DXGI_FORMAT_R16G16_FLOAT,false);
    Memory("before-AllocateParameters",w,h);
    NgxResult allocated=0;{ScopedSrwExclusive lock(&g_nr_api_lock);allocated=g_nr_api.allocate(&c.params);}
    std::printf("ALLOC result=0x%08X params=%p\n",allocated,c.params);Memory("after-AllocateParameters",w,h);
    Check(allocated==kNgxSuccess&&c.params,"actual parameters");
    NrGuideBinding guides{};guides.depth=m.depth;guides.motion=m.motion;guides.game_reset=1;
    guides.valid_motion_scale_x=guides.valid_motion_scale_y=true;
    auto& s=guides.signature;s.depth_width=s.depth_physical_width=2228;s.depth_height=s.depth_physical_height=1253;
    s.motion_source_width=s.motion_bound_width=s.motion_physical_width=s.color_physical_width=w;
    s.motion_source_height=s.motion_bound_height=s.motion_physical_height=s.color_physical_height=h;
    s.depth_format=DXGI_FORMAT_R32_FLOAT;s.motion_format=DXGI_FORMAT_R16G16_FLOAT;s.color_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    s.normalized_create_flags=73;s.depth_inverted=1;
    FeatureRecord feature{};feature.feature_id=1;feature.create_flags=73;
    Memory("before-BindNrParameters",w,h);
    Check(BindNrParameters(c,slot,guides,feature,w,h,ColorContract{},true),"production post parameter binding");
    Memory("after-BindNrParameters",w,h);
    int flags=0;unsigned dw=0,mw=0;float intensity=0,skin=0;int mask=1;unsigned ui=0;
    Check(GetInt(c.params,"DLSS.Feature.Create.Flags",&flags)&&flags==73,"flags 73");
    Check(GetUInt(c.params,"DLSSNR.DepthSubrectWidth",&dw)&&dw==2228&&GetUInt(c.params,"DLSSNR.MVecSubrectWidth",&mw)&&mw==w,"guide binding extents");
    Check(GetFloat(c.params,"DLSSNR.Intensity",&intensity)&&intensity==1.2f&&GetFloat(c.params,"DLSSNR.SkinStructureStrength",&skin)&&skin==-1.f,"historical game tuning");
    Check(GetInt(c.params,"DLSSNR.UseAutoMask",&mask)&&!mask&&GetUInt(c.params,"DLSSNR.UICorrection",&ui)&&ui==1,"historical mask/UI settings");
    std::printf("CONTRACT flags=%d depth=2228x1253/R32F motion=%ux%u/RG16F inputOutput=RGBA16F intensity=%.3f skin=%.3f mask=%d ui=%u; synthetic depth=1 motion=0 color=0; no Evaluate\n",flags,w,h,intensity,skin,mask,ui);
    io.Reset();void* handle=nullptr;Memory("before-CreateSignedNrFeature",w,h);
    const auto result=CreateSignedNrFeature(io.list,c,feature,&handle);
    std::printf("CREATE result=0x%08X handle=%p quarantine=%p\n",result,handle,c.quarantined_create_handle);Memory("after-CreateSignedNrFeature",w,h);
    c.nr_handle=static_cast<NgxHandle*>(handle);
    // Failed opaque Create can still have recorded commands. Complete them
    // before releasing anything, including the null-handle failure path.
    Memory("before-Create-submit",w,h);io.Submit();Memory("after-Create-fence",w,h);m.safe=true;
    Check(!c.create_dependencies_quarantined,"ambiguous Create retained until process exit");
    return result==kNgxSuccess&&handle;
}
void Release(Model& m) {
    Check(m.safe,"exact Create submission fence completed before Release");
    Memory("before-DestroyContext-Release",m.w,m.h);DestroyContext(*m.context);
    std::printf("RELEASE remainingHandle=%p remainingParams=%p quarantined=%d\n",m.context->nr_handle,m.context->params,m.context->lifecycle_dependencies_quarantined);
    Memory("after-DestroyContext-Release",m.w,m.h);
    Check(!m.context->nr_handle&&!m.context->params&&!m.context->lifecycle_dependencies_quarantined,"production retirement succeeded");
    ReleaseCom(m.depth);ReleaseCom(m.motion);delete m.context;m.context=nullptr;
}
}
int wmain(int argc,wchar_t** argv){
    using namespace scale_host;std::setvbuf(stdout,nullptr,_IONBF,0);std::setvbuf(stderr,nullptr,_IONBF,0);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Fault);
    try {
        Check(argc==3,"usage: scale-host runtime-directory serial|overlap");
        const bool overlap=std::wstring(argv[2])==L"overlap";Check(overlap||std::wstring(argv[2])==L"serial","bounded order");
        InitializeCriticalSection(&g_chain_cs);g_config=ChainConfig{};g_config.enabled=true;g_config.mode=1;g_config.nr_passes=1;
        g_enabled_fast.store(true);g_config_generation.store(1);
        swprintf_s(g_log_path,L"%ls\\production.log",argv[1]);swprintf_s(g_ini_path,L"%ls\\host.ini",argv[1]);
        IDXGIFactory4* factory=nullptr;IDXGIAdapter1* adapter=nullptr;ID3D12Device* device=nullptr;ID3D12CommandQueue* queue=nullptr;
        Hr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory)),"factory");
        for(unsigned i=0;IDXGIFactory4_EnumAdapters1(factory,i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};IDXGIAdapter1_GetDesc1(adapter,&d);
            if(d.VendorId==0x10de&&!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){std::printf("ADAPTER vendor=%x device=%x dedicated=%llu\n",d.VendorId,d.DeviceId,static_cast<unsigned long long>(d.DedicatedVideoMemory));break;}ReleaseCom(adapter);}
        Check(adapter,"real NVIDIA adapter");Hr(IDXGIAdapter1_QueryInterface(adapter,IID_IDXGIAdapter3,reinterpret_cast<void**>(&memory_adapter)),"memory adapter");
        Hr(D3D12CreateDevice(reinterpret_cast<IUnknown*>(adapter),D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void**>(&device)),"real device");
        Memory("before-EnsureNrApi");Check(EnsureNrApi(device),"production cold bootstrap");Memory("after-EnsureNrApi");
        D3D12_COMMAND_QUEUE_DESC q{};Hr(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,reinterpret_cast<void**>(&queue)),"queue");
        Io io;io.Init(device,queue);const unsigned widths[]={3840,3776,2720},heights[]={2160,2120,1528};
        Model* held=nullptr;unsigned successful=0,failed=0;
        for(unsigned i=0;i<3;++i){
            if(held&&!overlap){Release(*held);delete held;held=nullptr;}
            std::printf("CASE order=%ls step=%u liveOld=%d extent=%ux%u\n",argv[2],i,held!=nullptr,widths[i],heights[i]);
            auto* next=new Model;const bool ok=Create(io,*next,widths[i],heights[i]);successful+=ok;failed+=!ok;
            if(held){Release(*held);delete held;held=nullptr;}
            if(ok)held=next;else{Release(*next);delete next;}
        }
        if(held){Release(*held);delete held;}
        Memory("finished");io.Destroy();ReleaseCom(queue);ReleaseCom(device);ReleaseCom(memory_adapter);ReleaseCom(adapter);ReleaseCom(factory);
        std::printf("COMPLETE order=%ls createsSucceeded=%u createsFailed=%u; real signed Create/Release only, artificial contract, no game acceptance\n",argv[2],successful,failed);return failed?2:0;
    }catch(const std::exception& e){std::fprintf(stderr,"HOST_FAIL %s; unreleased dependencies retained until process exit\n",e.what());return 1;}
}
