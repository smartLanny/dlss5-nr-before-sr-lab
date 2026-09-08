// Isolated Synthetic consumer, not a Core provider or production cascade.
#define NR_CASCADE_FAIL_CLOSED
#define wmain unused_direct_sr_wmain
#include "../direct_sr_gpu.cpp"
#undef wmain
#include <array>
namespace {
using Result=std::uint32_t;
[[noreturn]] void Stop(const char* why) { FatalSubmittedGpu(why); }
void Ngx(Result r,const char* label) {
    std::cout<<label<<"=0x"<<std::hex<<r<<std::dec<<std::endl;
    if(r!=1u) Stop(label); // Failed Create may return a handle: no cleanup or retry.
}
void* Symbol(HMODULE m,const char* name) {
    auto p=reinterpret_cast<void*>(GetProcAddress(m,name)); if(!p) Stop(name); return p;
}
HMODULE Module(const std::wstring& path) {
    auto m=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!m) Stop("module load");
    wchar_t actual[32768]{};GetModuleFileNameW(m,actual,32768);
    std::wcout<<L"loaded="<<actual<<std::endl;return m; // Pinned until process exit.
}
Result Ratio(NVSDK_NGX_Parameter* p) {p->Set("DLSSNR.ScalingRatio",1.f);return 1;}
void Bind(NVSDK_NGX_Parameter* p,ID3D12Resource* input,ID3D12Resource* output,
          ID3D12Resource* depth,ID3D12Resource* motion,unsigned layer) {
    // Official MSVC 17-slot Parameter interface, allocated by the native factory.
    p->Set("DLSSNR.Color",input);p->Set("DLSSNR.Output",output);
    p->Set("DLSSNR.Depth",depth);p->Set("DLSSNR.MVec",motion);
    for(const char* k:{"Width","OutWidth","DLSSNR.Width","DLSSNR.InputWidth","DLSSNR.OutputWidth","DLSSNR.Output.Width"}) p->Set(k,kOutputWidth);
    for(const char* k:{"Height","OutHeight","DLSSNR.Height","DLSSNR.InputHeight","DLSSNR.OutputHeight","DLSSNR.Output.Height"}) p->Set(k,kOutputHeight);
    for(const char* name:{"Color","Depth","MVec","Output"}) {
        const std::string pfx=std::string("DLSSNR.")+name+"Subrect";
        p->Set((pfx+"BaseX").c_str(),0u);p->Set((pfx+"BaseY").c_str(),0u);
        p->Set((pfx+"Width").c_str(),kOutputWidth);p->Set((pfx+"Height").c_str(),kOutputHeight);
    }
    p->Set("DLSSNRComputeScalingRatioCallback",reinterpret_cast<void*>(&Ratio));
    for(const char* k:{"DLSSNR.ScalingRatio","DLSSNR.Scale","DLSSNR.MVecScaleX","DLSSNR.MVecScaleY","DLSS.Pre.Exposure","DLSS.Exposure.Scale","DLSSNR.Jitter.Scale.X","DLSSNR.Jitter.Scale.Y"})p->Set(k,1.f);
    p->Set("ExposureTexture",static_cast<ID3D12Resource*>(nullptr));
    p->Set("DLSSNR.Upscaling",0);p->Set("PerfQualityValue",5);p->Set("DLSS.Feature.Create.Flags",1);
    p->Set("CreationNodeMask",1u);p->Set("VisibilityNodeMask",1u);
    p->Set("DLSSNR.DepthInverted",0);p->Set("DLSSNR.Enabled",1);p->Set("DLSSNR.Reset",1);
    for(const char* k:{"Jitter.Offset.X","Jitter.Offset.Y","DLSSNR.Jitter.Offset.X","DLSSNR.Jitter.Offset.Y"})p->Set(k,0.f);
    p->Set("DLSSNR.Jitter.Valid",layer?0:1); // Later-layer phase is unverified despite reset/zero values.
    p->Set("DLSSNR.Jitter.SourceWidth",kOutputWidth);p->Set("DLSSNR.Jitter.WorkWidth",kOutputWidth);
    p->Set("DLSSNR.Jitter.SourceHeight",kOutputHeight);p->Set("DLSSNR.Jitter.WorkHeight",kOutputHeight);
    p->Set("DLSSNR.Intensity",1.f);p->Set("DLSSNR.LocalToneStrength",0.f);
    p->Set("DLSSNR.LocalStructureStrength",0.f);p->Set("DLSSNR.SkinStructureStrength",0.f);
    p->Set("DLSSNR.UseAutoMask",0);p->Set("DLSSNR.Style",0u);p->Set("DLSSNR.UICorrection",0u);
    p->Set("DLSS.Indicator.Invert.X.Axis",0);p->Set("DLSS.Indicator.Invert.Y.Axis",0);
    ID3D12Resource *bound_in=nullptr,*bound_out=nullptr;int reset=0;unsigned width=0;
    Ngx(p->Get("DLSSNR.Color",&bound_in),"get_input");Ngx(p->Get("DLSSNR.Output",&bound_out),"get_output");
    Ngx(p->Get("DLSSNR.Reset",&reset),"get_reset");Ngx(p->Get("Width",&width),"get_width");
    if(bound_in!=input||bound_out!=output||input==output||reset!=1||width!=kOutputWidth)Stop("native parameter roundtrip");
}
void Barrier(DeviceContext& c,ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};c.list->ResourceBarrier(1,&b);
}
void Experiment(const std::wstring& sdk_dir,const std::wstring& nr_dir) {
    auto* c=new DeviceContext(MakeDevice(false)); // Deliberately process-owned.
    constexpr auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    std::array<ComPtr<ID3D12Resource>,3> color;
    for(auto& r:color)r=Texture(c->device.Get(),kOutputWidth,kOutputHeight,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
    auto depth=Texture(c->device.Get(),kOutputWidth,kOutputHeight,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto motion=Texture(c->device.Get(),kOutputWidth,kOutputHeight,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    std::vector<uint16_t> pixels(size_t(kOutputWidth)*kOutputHeight*4),mv(size_t(kOutputWidth)*kOutputHeight*2,0);
    std::vector<float> z(size_t(kOutputWidth)*kOutputHeight,0.5f);
    for(size_t i=0;i<pixels.size()/4;++i) {
        pixels[i*4]=Half(0.1f+0.4f*float(i%kOutputWidth)/kOutputWidth);
        pixels[i*4+1]=Half(0.2f);pixels[i*4+2]=Half(0.3f);pixels[i*4+3]=Half(1.f);
    }
    std::vector<ComPtr<ID3D12Resource>> uploads;
    for(auto& r:color)Upload(*c,r.Get(),pixels.data(),kOutputWidth*8,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,uploads);
    Upload(*c,depth.Get(),z.data(),kOutputWidth*4,read,uploads);Upload(*c,motion.Get(),mv.data(),kOutputWidth*4,read,uploads);
    c->SubmitAndWait();c->Reset();const auto baseline=ReadOutput(*c,color[0].Get());c->Reset();
    std::cout<<"P0_initial_hash="<<baseline.hash<<std::endl;
    uint64_t input_hash=baseline.hash;
    Barrier(*c,color[0].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,read);c->SubmitAndWait();c->Reset();
    const wchar_t* paths[]={sdk_dir.c_str()};NVSDK_NGX_FeatureCommonInfo common{};common.PathListInfo.Path=paths;common.PathListInfo.Length=1;
    Ngx(NVSDK_NGX_D3D12_Init_with_ProjectID("7a94da60-f8c9-4b8b-9d8a-22f0293c4801",NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        "synthetic-two-nr-unverified-phase",sdk_dir.c_str(),c->device.Get(),&common,NVSDK_NGX_Version_API),"bootstrap_init");
    auto core=GetModuleHandleW(L"_nvngx.dll");if(!core)Stop("official factory unavailable");
    wchar_t factory_path[32768]{};
    if(!GetModuleFileNameW(core,factory_path,32768))Stop("factory path receipt");
    std::wcout<<L"loaded_factory="<<factory_path<<std::endl;
    auto nr=Module(nr_dir+L"\\nvngx_dlssnr.dll"),bridge=Module(nr_dir+L"\\nrchain_nvngx.dll");
    auto init=reinterpret_cast<Result(*)(void*,unsigned long long,const wchar_t*,ID3D12Device*,uint32_t,const void*)>(Symbol(bridge,"NRChain_Init"));
    auto create=reinterpret_cast<Result(*)(void*,ID3D12GraphicsCommandList*,int,void*,void**)>(Symbol(bridge,"NRChain_Create"));
    auto evaluate=reinterpret_cast<Result(*)(void*,ID3D12GraphicsCommandList*,const void*,const void*,void*)>(Symbol(bridge,"NRChain_Evaluate"));
    auto release=reinterpret_cast<Result(*)(void*,void*)>(Symbol(bridge,"NRChain_Release"));
    auto allocate=reinterpret_cast<Result(*)(NVSDK_NGX_Parameter**)>(Symbol(core,"NVSDK_NGX_D3D12_AllocateParameters"));
    auto destroy=reinterpret_cast<Result(*)(NVSDK_NGX_Parameter*)>(Symbol(core,"NVSDK_NGX_D3D12_DestroyParameters"));
    Ngx(init(Symbol(nr,"NVSDK_NGX_D3D12_Init_Ext"),0xE658703ull,nr_dir.c_str(),c->device.Get(),19,nullptr),"nr_init");
    std::array<NVSDK_NGX_Parameter*,2> params{};std::array<void*,2> features{};
    for(unsigned layer=0;layer<2;++layer) {
        Ngx(allocate(&params[layer]),"allocate");if(!params[layer]||(layer&&params[0]==params[1]))Stop("params identity");
        Bind(params[layer],color[layer].Get(),color[layer+1].Get(),depth.Get(),motion.Get(),layer);
        Ngx(create(Symbol(nr,"NVSDK_NGX_D3D12_CreateFeature"),c->list.Get(),18,params[layer],&features[layer]),"create");
        if(!features[layer]||(layer&&features[0]==features[1]))Stop("feature identity");
        c->SubmitAndWait();c->Reset();
        std::cout<<"layer="<<layer<<" reset=1 synthetic=true phase="<<(layer?"unverified":"known-static-zero")<<std::endl;
        Ngx(evaluate(Symbol(nr,"NVSDK_NGX_D3D12_EvaluateFeature"),c->list.Get(),features[layer],params[layer],nullptr),"evaluate");
        c->SubmitAndWait();c->Reset();const auto s=ReadOutput(*c,color[layer+1].Get());c->Reset();
        std::cout<<"layer="<<layer<<" fence="<<c->next_fence-1<<" finite="<<s.finite<<" nonfinite="<<s.nonfinite<<" hash="<<s.hash<<std::endl;
        std::cout<<"layer="<<layer<<" input_hash="<<input_hash<<" output_hash="<<s.hash<<" changed="<<(input_hash!=s.hash)<<" quality_claim=false"<<std::endl;
        input_hash=s.hash;
        if(s.nonfinite||s.finite!=pixels.size())Stop("finite output");
        if(layer==0){Barrier(*c,color[1].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,read);c->SubmitAndWait();c->Reset();}
    }
    Barrier(*c,color[0].Get(),read,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto preserved=ReadOutput(*c,color[0].Get());c->Reset();
    std::cout<<"P0_final_hash="<<preserved.hash<<std::endl;
    if(preserved.hash!=baseline.hash)Stop("P0 changed");
    for(unsigned i=0;i<2;++i){Ngx(release(Symbol(nr,"NVSDK_NGX_D3D12_ReleaseFeature"),features[i]),"release");Ngx(destroy(params[i]),"destroy");}
    Ngx(NVSDK_NGX_D3D12_Shutdown1(c->device.Get()),"bootstrap_shutdown");
    std::cout<<"complete layers=2 frames=1 P0_preserved=true phase_verified=false production=false composite=not-executed"<<std::endl;
    ExitProcess(0); // Also retains signed Init dependencies until process termination.
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=4||wcscmp(argv[1],L"--synthetic-static-two-feature")!=0){
        std::cerr<<"usage: two_feature_gpu.exe --synthetic-static-two-feature <absolute NGX runtime dir> <absolute NR bundle dir>\n";return 1;
    }
    std::set_terminate([]{Stop("unexpected exception");});Experiment(argv[2],argv[3]);
}
