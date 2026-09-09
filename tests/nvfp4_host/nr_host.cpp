// Real signed Feature18 on an owned D3D12 queue. Synthetic inputs, not a game test.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef NR_HOST_BACKEND
#include "src/nvfp4/backend.h"
#endif
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
#ifdef NR_HOST_BACKEND
struct Observation {
    fs::path directory;std::mutex mutex;unsigned launches=0;
    ComPtr<ID3D12QueryHeap> query;ComPtr<ID3D12Resource> readback;UINT count=0;UINT64 frequency=0;
    struct Point{nr::nvfp4::Stage stage;bool begin;unsigned block;};std::array<Point,128> points{};
    static void Stage(nr::nvfp4::Stage stage,bool begin,uint32_t block,ID3D12GraphicsCommandList*list,void*user){auto&self=*static_cast<Observation*>(user);if(!self.query||self.count==self.points.size())return;
        self.points[self.count]={stage,begin,block};list->EndQuery(self.query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,self.count++);}
    std::array<double,6> Times(){std::array<double,6> result{};if(!count)return result;void*data=nullptr;D3D12_RANGE range{0,SIZE_T(count)*8};if(FAILED(readback->Map(0,&range,&data)))throw std::runtime_error("stage timestamp map");
        auto*t=static_cast<uint64_t*>(data);for(UINT i=0;i<count;++i){if(!points[i].begin)continue;for(UINT j=i+1;j<count;++j){if(points[i].stage==points[j].stage&&points[i].block==points[j].block&&!points[j].begin){if(t[j]<t[i])throw std::runtime_error("nonmonotonic stage timestamps");result[unsigned(points[i].stage)]+=double(t[j]-t[i])*1000.0/frequency;break;}}}
        D3D12_RANGE written{0,0};readback->Unmap(0,&written);return result;}
    static void Module(const void*data,uint32_t bytes,const uint8_t hash[32],void*user){auto&self=*static_cast<Observation*>(user);std::lock_guard lock(self.mutex);char name[65]{};
        for(unsigned i=0;i<32;++i)snprintf(name+i*2,3,"%02x",hash[i]);std::ofstream out(self.directory/(std::string(name)+".cubin"),std::ios::binary);out.write(static_cast<const char*>(data),bytes);}
    static void Launch(const char*name,const void*params,uint32_t bytes,const uint32_t grid[3],const uint32_t block[3],uint32_t shared,void*user){auto&self=*static_cast<Observation*>(user);std::lock_guard lock(self.mutex);if(self.launches>=16)return;auto index=self.launches++;
        std::ofstream raw(self.directory/("launch-"+std::to_string(index)+".bin"),std::ios::binary);raw.write(static_cast<const char*>(params),bytes);
        std::ofstream out(self.directory/"launches.jsonl",std::ios::app);out<<"{\"index\":"<<index<<",\"function\":\""<<name<<"\",\"params_bytes\":"<<bytes<<",\"grid\":["<<grid[0]<<','<<grid[1]<<','<<grid[2]<<"],\"block\":["<<block[0]<<','<<block[1]<<','<<block[2]<<"],\"shared\":"<<shared<<"}\n";}
};
#endif
using Result = uint32_t;
struct Params { void** vtable; };
template<class T> T Entry(HMODULE m, const char* name) {
    auto p=GetProcAddress(m,name); if(!p) throw std::runtime_error(std::string("missing export: ")+name);
    return reinterpret_cast<T>(p);
}
template<class T> void Set(Params* p,unsigned slot,const char* name,T v) {
    reinterpret_cast<void(*)(Params*,const char*,T)>(p->vtable[slot])(p,name,v);
}
void UInt(Params*p,const char*k,unsigned v){Set(p,4,k,v);}
void Int(Params*p,const char*k,int v){Set(p,3,k,v);}
void Float(Params*p,const char*k,float v){Set(p,6,k,v);}
void Resource(Params*p,const char*k,ID3D12Resource*v){Set(p,1,k,v);}
void Check(HRESULT r,const char* what){if(FAILED(r))throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string(uint32_t(r)));}
void Ngx(Result r,const char*what){std::cerr<<what<<"=0x"<<std::hex<<r<<std::dec<<'\n';if(r!=1&&r!=0xBAD00003u)throw std::runtime_error(what);}
[[noreturn]] void GpuFailure(const char*why){std::cerr<<"submitted GPU work uncertain: "<<why<<'\n';std::cerr.flush();ExitProcess(3);}
struct Device {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence; HANDLE event=nullptr;uint64_t serial=0;
    Device(){ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
        for(UINT i=0;;++i){ComPtr<IDXGIAdapter1>a;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND)break;
            DXGI_ADAPTER_DESC1 d{};a->GetDesc1(&d);if(d.VendorId!=0x10de||(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))continue;
            if(SUCCEEDED(D3D12CreateDevice(a.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)))){std::wcerr<<L"adapter="<<d.Description<<L'\n';break;}}
        if(!device)throw std::runtime_error("NVIDIA D3D12 device unavailable");D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");Check(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)),"allocator");
        Check(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("event");}
    ~Device(){if(event)CloseHandle(event);}
    void Submit(uint64_t token=0){Check(list->Close(),"close");ID3D12CommandList*l[]={list.Get()};queue->ExecuteCommandLists(1,l);
        const auto value=++serial;if(FAILED(queue->Signal(fence.Get(),value)))GpuFailure("signal");
#ifdef NR_HOST_BACKEND
        if(token)nr::nvfp4::OnSubmit(token,queue.Get(),fence.Get(),value);
#else
        (void)token;
#endif
        if(FAILED(fence->SetEventOnCompletion(value,event)))GpuFailure("event arm");
        if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0||fence->GetCompletedValue()==UINT64_MAX||fence->GetCompletedValue()<value)GpuFailure("timeout/device removed");
        Check(allocator->Reset(),"allocator reset");Check(list->Reset(allocator.Get(),nullptr),"list reset");
#ifdef NR_HOST_BACKEND
        if(token)nr::nvfp4::OnReset(token,false);
#endif
    }
};
ComPtr<ID3D12Resource> Buffer(Device&d,uint64_t bytes,D3D12_HEAP_TYPE kind){D3D12_HEAP_PROPERTIES h{};h.Type=kind;D3D12_RESOURCE_DESC r{};
    r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=bytes;r.Height=1;r.DepthOrArraySize=1;r.MipLevels=1;r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource>b;Check(d.device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,kind==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&b)),"buffer");return b;}
ComPtr<ID3D12Resource> Texture(Device&d,UINT w,UINT h,DXGI_FORMAT f){D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC r{};
    r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=w;r.Height=h;r.DepthOrArraySize=1;r.MipLevels=1;r.Format=f;r.SampleDesc.Count=1;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource>out;Check(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&out)),"texture");return out;}
void Barrier(Device&d,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};d.list->ResourceBarrier(1,&x);}
uint16_t Half(float value){uint32_t x;memcpy(&x,&value,4);uint32_t sign=(x>>16)&0x8000;int e=int((x>>23)&255)-127+15;uint32_t m=x&0x7fffff;
    if(e<=0){if(e<-10)return uint16_t(sign);m|=0x800000;const unsigned shift=unsigned(14-e);const uint32_t out=m>>shift,rem=m&((1u<<shift)-1);return uint16_t(sign+out+(rem>(1u<<(shift-1))||(rem==(1u<<(shift-1))&&(out&1))));}
    if(e>=31)return uint16_t(sign|0x7c00);uint32_t out=(uint32_t(e)<<10)|(m>>13);out+=(m&8191)>4096||((m&8191)==4096&&(out&1));return uint16_t(sign|out);}
float Unhalf(uint16_t h){const float sign=(h&0x8000)?-1.f:1.f;const unsigned e=(h>>10)&31,m=h&1023;if(e==31)return m?NAN:sign*INFINITY;return sign*std::ldexp(float(e?1024+m:m),e?int(e)-25:-24);}
void Upload(Device&d,ID3D12Resource*r,unsigned channels,bool depth){auto desc=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows;UINT64 row,bytes;d.device->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&row,&bytes);
    auto upload=Buffer(d,bytes,D3D12_HEAP_TYPE_UPLOAD);void*m=nullptr;Check(upload->Map(0,nullptr,&m),"upload map");memset(m,0,size_t(bytes));
    for(UINT y=0;y<desc.Height;++y){auto*p=static_cast<uint8_t*>(m)+fp.Offset+uint64_t(y)*fp.Footprint.RowPitch;for(UINT x=0;x<desc.Width;++x){if(depth){float v=0.25f+0.5f*float(y)/desc.Height;memcpy(p+x*4,&v,4);}else for(unsigned c=0;c<channels;++c){float v=0.f;if(channels==4)v=c==3?1.f:(0.1f+0.6f*float((x/16+y/16+c*3)%17)/16.f);auto h=Half(v);memcpy(p+(x*channels+c)*2,&h,2);}}}
    upload->Unmap(0,nullptr);Barrier(d,r,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=r;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;d.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    Barrier(d,r,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);d.Submit();}
void Dump(Device&d,ID3D12Resource*r,const fs::path&path){const auto desc=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows;UINT64 row,bytes;d.device->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&row,&bytes);
    auto out=Buffer(d,bytes,D3D12_HEAP_TYPE_READBACK);Barrier(d,r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=r;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=out.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;d.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(d,r,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);d.Submit();
    void*m;D3D12_RANGE read{0,SIZE_T(bytes)};Check(out->Map(0,&read,&m),"readback map");std::ofstream f(path,std::ios::binary);if(!f)throw std::runtime_error("output file");size_t bad=0;double sum=0;
    for(UINT y=0;y<desc.Height;++y){auto*p=static_cast<uint8_t*>(m)+fp.Offset+uint64_t(y)*fp.Footprint.RowPitch;f.write(reinterpret_cast<char*>(p),std::streamsize(row));for(size_t x=0;x<row/2;++x){uint16_t h;memcpy(&h,p+x*2,2);float v=Unhalf(h);if(!std::isfinite(v))++bad;else sum+=v;}}
    D3D12_RANGE written{0,0};out->Unmap(0,&written);std::cerr<<"output_nonfinite="<<bad<<" sum="<<sum<<'\n';if(bad)throw std::runtime_error("nonfinite NR output");}
void Bind(Params*p,unsigned w,unsigned h,ID3D12Resource*c,ID3D12Resource*z,ID3D12Resource*mv,ID3D12Resource*out){
    Resource(p,"DLSSNR.Color",c);Resource(p,"DLSSNR.Output",out);Resource(p,"DLSSNR.Depth",z);Resource(p,"DLSSNR.MVec",mv);
    for(auto k:{"Width","OutWidth","DLSSNR.Width","DLSSNR.InputWidth","DLSSNR.OutputWidth","DLSSNR.Output.Width","DLSSNR.ColorSubrectWidth","DLSSNR.DepthSubrectWidth","DLSSNR.MVecSubrectWidth","DLSSNR.OutputSubrectWidth"})UInt(p,k,w);
    for(auto k:{"Height","OutHeight","DLSSNR.Height","DLSSNR.InputHeight","DLSSNR.OutputHeight","DLSSNR.Output.Height","DLSSNR.ColorSubrectHeight","DLSSNR.DepthSubrectHeight","DLSSNR.MVecSubrectHeight","DLSSNR.OutputSubrectHeight"})UInt(p,k,h);
    for(auto k:{"DLSSNR.ColorSubrectBaseX","DLSSNR.ColorSubrectBaseY","DLSSNR.DepthSubrectBaseX","DLSSNR.DepthSubrectBaseY","DLSSNR.MVecSubrectBaseX","DLSSNR.MVecSubrectBaseY","DLSSNR.OutputSubrectBaseX","DLSSNR.OutputSubrectBaseY"})UInt(p,k,0);
    UInt(p,"CreationNodeMask",1);UInt(p,"VisibilityNodeMask",1);Int(p,"PerfQualityValue",5);Int(p,"DLSS.Feature.Create.Flags",10);Int(p,"DLSSNR.DepthInverted",1);Int(p,"DLSSNR.Enabled",1);Int(p,"DLSSNR.Upscaling",0);
    Float(p,"DLSSNR.ScalingRatio",1);Float(p,"DLSSNR.Scale",1);Float(p,"DLSSNR.MV.Scale.X",1);Float(p,"DLSSNR.MV.Scale.Y",1);Float(p,"MV.Scale.X",1);Float(p,"MV.Scale.Y",1);
    Float(p,"DLSS.Pre.Exposure",1);Float(p,"DLSS.Exposure.Scale",1);Float(p,"DLSSNR.Intensity",1.2f);Float(p,"DLSSNR.LocalToneStrength",1);Float(p,"DLSSNR.LocalStructureStrength",1);Float(p,"DLSSNR.SkinStructureStrength",1);
    Int(p,"DLSSNR.UseAutoMask",0);UInt(p,"DLSSNR.Style",0);UInt(p,"DLSSNR.UICorrection",1);Int(p,"DLSSNR.Reset",1);
}
int wmain(int argc,wchar_t**argv){try{
    if(argc<7){std::cerr<<"nr_host <NR dll> <bridge dll> <asset dir> <output dir> <width> <height> [frames=16] [mode=fp8] [warmup=8]\n";return 1;}
    const fs::path runtime=fs::absolute(argv[1]),bridge=fs::absolute(argv[2]),assets=fs::absolute(argv[3]),output=fs::absolute(argv[4]);
    const unsigned w=std::stoul(argv[5]),h=std::stoul(argv[6]),frames=argc>7?std::stoul(argv[7]):16,warmup=argc>9?std::stoul(argv[9]):8;
    if(!w||!h||w>7680||h>4320||frames>100000||warmup>10000)throw std::runtime_error("invalid dimensions/frame count");
    const std::wstring mode=argc>8?argv[8]:L"fp8";
    if(mode!=L"fp8"&&mode!=L"passthrough"&&mode!=L"reference"&&mode!=L"optimized"&&mode!=L"fused"&&mode!=L"two")throw std::runtime_error("unknown host experiment mode");
    fs::create_directories(output);Device d;
#ifdef NR_HOST_BACKEND
    nr::nvfp4::Config cfg;cfg.asset_directory=assets.c_str();cfg.nr_runtime_path=runtime.c_str();cfg.mode=mode==L"fp8"?nr::nvfp4::Mode::Disabled:mode==L"passthrough"?nr::nvfp4::Mode::Passthrough:mode==L"reference"?nr::nvfp4::Mode::UpstreamReference:nr::nvfp4::Mode::Optimized;
    Observation observation;observation.directory=output;cfg.observe_module=&Observation::Module;cfg.observe_launch=&Observation::Launch;cfg.observe_stage=&Observation::Stage;cfg.observer_user=&observation;
    nr::nvfp4::OptimizedAcceptance acceptance;std::wstring optimized_dir;std::array<std::string,4> accepted_strings;std::vector<uint32_t> accepted_m;
    nr::nvfp4::ExpansionAcceptance expansion_acceptance;std::wstring expansion_dir;std::array<std::string,4> expansion_strings;
    if(mode==L"optimized"||mode==L"fused"||mode==L"two"){
        if(argc<11)throw std::runtime_error("optimized mode requires a privately verified acceptance text file");std::ifstream f{fs::path(argv[10])};if(!f)throw std::runtime_error("acceptance file missing");
        std::string line;std::getline(f,line);optimized_dir=fs::u8path(line).wstring();for(auto&s:accepted_strings)if(!std::getline(f,s))throw std::runtime_error("incomplete acceptance file");
        std::getline(f,line);acceptance.variant=unsigned(std::stoul(line));while(std::getline(f,line)){if(!line.empty())accepted_m.push_back(unsigned(std::stoul(line)));}if(accepted_m.empty())throw std::runtime_error("no accepted shapes");
        acceptance.directory=optimized_dir.c_str();acceptance.cubin_sha256=accepted_strings[0].c_str();acceptance.builder_sha256=accepted_strings[1].c_str();acceptance.build_id=accepted_strings[2].c_str();acceptance.kernel_id=accepted_strings[3].c_str();acceptance.tested_m=accepted_m.data();acceptance.tested_m_count=unsigned(accepted_m.size());cfg.optimized_acceptance=&acceptance;
        acceptance.fused_publish=mode==L"fused"||mode==L"two";
        if(mode==L"two"){
            if(argc<12)throw std::runtime_error("two-launch mode requires an explicit expansion receipt");
            std::ifstream ef{fs::path(argv[11])};if(!ef)throw std::runtime_error("expansion receipt missing");
            std::getline(ef,line);expansion_dir=fs::u8path(line).wstring();for(auto&s:expansion_strings)if(!std::getline(ef,s))throw std::runtime_error("incomplete expansion receipt");
            if(!std::getline(ef,line))throw std::runtime_error("missing expansion variant");
            expansion_acceptance.directory=expansion_dir.c_str();expansion_acceptance.cubin_sha256=expansion_strings[0].c_str();
            expansion_acceptance.builder_sha256=expansion_strings[1].c_str();expansion_acceptance.build_id=expansion_strings[2].c_str();
            expansion_acceptance.kernel_id=expansion_strings[3].c_str();expansion_acceptance.variant=unsigned(std::stoul(line));
            acceptance.expansion=&expansion_acceptance;
        }
    }
    if(!nr::nvfp4::Init(cfg)||(cfg.mode!=nr::nvfp4::Mode::Disabled&&!nr::nvfp4::EnsureStarted(d.device.Get())))throw std::runtime_error("backend startup");
#else
    if(mode!=L"fp8")throw std::runtime_error("this host build has no backend");
#endif
    wchar_t driver[MAX_PATH]{};DWORD n=sizeof(driver);Check(HRESULT_FROM_WIN32(RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore",L"FullPath",RRF_RT_REG_SZ,nullptr,driver,&n)),"NGX driver path");
    const auto core=LoadLibraryExW((fs::path(driver)/L"_nvngx.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const auto nr=LoadLibraryExW(runtime.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const auto caller=LoadLibraryExW(bridge.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!core||!nr||!caller)throw std::runtime_error("runtime LoadLibrary failed: "+std::to_string(GetLastError()));
    auto init=Entry<Result(*)(void*,uint64_t,const wchar_t*,ID3D12Device*,uint32_t,const void*)>(caller,"NRChain_Init");
    auto create=Entry<Result(*)(void*,ID3D12GraphicsCommandList*,int,void*,void**)>(caller,"NRChain_Create");
    auto evaluate=Entry<Result(*)(void*,ID3D12GraphicsCommandList*,const void*,const void*,void*)>(caller,"NRChain_Evaluate");
    auto release=Entry<Result(*)(void*,void*)>(caller,"NRChain_Release");auto allocate=Entry<Result(*)(Params**)>(core,"NVSDK_NGX_D3D12_AllocateParameters");
    auto destroy=Entry<Result(*)(Params*)>(core,"NVSDK_NGX_D3D12_DestroyParameters");
    auto signedInit=Entry<void*>(nr,"NVSDK_NGX_D3D12_Init_Ext"),signedCreate=Entry<void*>(nr,"NVSDK_NGX_D3D12_CreateFeature"),signedEval=Entry<void*>(nr,"NVSDK_NGX_D3D12_EvaluateFeature"),signedRelease=Entry<void*>(nr,"NVSDK_NGX_D3D12_ReleaseFeature");
    Ngx(init(signedInit,0xE658703ull,output.c_str(),d.device.Get(),19,nullptr),"init");Params*p=nullptr;Ngx(allocate(&p),"allocate");if(!p)throw std::runtime_error("null params");
    auto c=Texture(d,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT),z=Texture(d,w,h,DXGI_FORMAT_R32_FLOAT),mv=Texture(d,w,h,DXGI_FORMAT_R16G16_FLOAT),out=Texture(d,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT);
    Upload(d,c.Get(),4,false);Upload(d,z.Get(),1,true);Upload(d,mv.Get(),2,false);Barrier(d,out.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Bind(p,w,h,c.Get(),z.Get(),mv.Get(),out.Get());void*handle=nullptr;Result created;
#ifdef NR_HOST_BACKEND
    nr::nvfp4::OwnedCall call;call.phase=nr::nvfp4::Phase::Create;call.context_id=1;call.generation=1;call.recording_token=100;call.device=d.device.Get();call.command_list=d.list.Get();call.width=w;call.height=h;
    {nr::nvfp4::OwnedScope scope(call);created=create(signedCreate,d.list.Get(),18,p,&handle);}
#else
    created=create(signedCreate,d.list.Get(),18,p,&handle);
#endif
    d.Submit(100);Ngx(created,"create");if(!handle)throw std::runtime_error("null handle");
    ComPtr<ID3D12QueryHeap>qh;D3D12_QUERY_HEAP_DESC qd{};qd.Count=2;qd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;Check(d.device->CreateQueryHeap(&qd,IID_PPV_ARGS(&qh)),"timestamps");auto times=Buffer(d,16,D3D12_HEAP_TYPE_READBACK);UINT64 frequency;Check(d.queue->GetTimestampFrequency(&frequency),"timestamp frequency");
#ifdef NR_HOST_BACKEND
    qd.Count=128;Check(d.device->CreateQueryHeap(&qd,IID_PPV_ARGS(&observation.query)),"stage timestamps");observation.readback=Buffer(d,128*8,D3D12_HEAP_TYPE_READBACK);observation.frequency=frequency;
    bool prior_replacement=false;
#endif
    const bool candidate=mode==L"reference"||mode==L"optimized"||mode==L"fused"||mode==L"two";
    bool epoch_started=!candidate;unsigned preparation_frames=0;
    std::ofstream timing(output/"timing.jsonl");for(unsigned i=0,submission=0;i<frames+warmup;++submission){Int(p,"DLSSNR.Reset",!epoch_started||i==0);d.list->EndQuery(qh.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);Result evaluated;uint64_t pairs=0,observed=0,original_launches=0,replacement_launches=0,uav_barriers=0;std::string reason="disabled";
#ifdef NR_HOST_BACKEND
        observation.count=0;
        call.phase=nr::nvfp4::Phase::Evaluate;call.recording_token=101+submission;call.evaluation_serial=submission+1;
        {nr::nvfp4::OwnedScope scope(call);if(!epoch_started&&scope.WillReplace())epoch_started=true;if(scope.WillReplace()!=prior_replacement)Int(p,"DLSSNR.Reset",1);evaluated=evaluate(signedEval,d.list.Get(),handle,p,nullptr);const auto status=scope.Status();pairs=status.replaced_pairs;observed=status.observed_m;original_launches=status.original_launches;replacement_launches=status.replacement_launches;uav_barriers=status.uav_barriers;reason=status.reason;if(evaluated==1)prior_replacement=pairs>0;if(scope.MayHaveRecordedFailure())GpuFailure("backend partial recording");}
#else
        evaluated=evaluate(signedEval,d.list.Get(),handle,p,nullptr);
#endif
        d.list->EndQuery(qh.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);d.list->ResolveQueryData(qh.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,times.Get(),0);
#ifdef NR_HOST_BACKEND
        if(observation.count)d.list->ResolveQueryData(observation.query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,observation.count,observation.readback.Get(),0);
#endif
        d.Submit(101+submission);if(evaluated!=1)Ngx(evaluated,"evaluate");
        if(epoch_started&&candidate&&pairs!=8)throw std::runtime_error("candidate left the fully prepared measurement epoch");
        void*ptr;D3D12_RANGE range{0,16};Check(times->Map(0,&range,&ptr),"timestamp map");uint64_t ticks[2];memcpy(ticks,ptr,16);D3D12_RANGE written{0,0};times->Unmap(0,&written);if(ticks[1]<ticks[0])throw std::runtime_error("nonmonotonic timestamps");
        const double ms=double(ticks[1]-ticks[0])*1000.0/frequency;timing<<"{\"frame\":"<<i<<",\"submission\":"<<submission<<",\"phase\":\""<<(!epoch_started?"preparing":i<warmup?"warmup":"measured")<<"\",\"warmup\":"<<(!epoch_started||i<warmup?"true":"false")<<",\"gpu_complete\":true,\"feature18_ms\":"<<ms<<",\"replaced_pairs\":"<<pairs<<",\"M\":"<<observed<<",\"original_launches\":"<<original_launches<<",\"replacement_launches\":"<<replacement_launches<<",\"uav_barriers\":"<<uav_barriers;
#ifdef NR_HOST_BACKEND
        const auto stage_times=observation.Times();if(observation.count)timing<<",\"ffn_ms\":"<<(pairs?stage_times[1]:stage_times[0])<<",\"pack_ms\":"<<stage_times[2]<<",\"expansion_ms\":"<<stage_times[3]<<",\"contraction_ms\":"<<stage_times[4]<<",\"epilogue_ms\":"<<stage_times[5];
#endif
        timing<<"}\n";
        if(submission<3||(epoch_started&&(i<3||i%100==0)))std::cerr<<"frame="<<i<<" phase="<<(!epoch_started?"preparing":i<warmup?"warmup":"measured")<<" feature18_ms="<<ms<<" replaced_pairs="<<pairs<<" M="<<observed<<" reason="<<reason<<'\n';
        if(epoch_started)++i;else if(++preparation_frames>=4096)throw std::runtime_error("candidate preparation did not complete within the bounded warmup");}
    Dump(d,out.Get(),output/"nr_output.f16");Ngx(release(signedRelease,handle),"release");Ngx(destroy(p),"destroy params");
#ifdef NR_HOST_BACKEND
    nr::nvfp4::RetireContext(1,1);nr::nvfp4::Shutdown();
#endif
    std::ofstream meta(output/"host.json");meta<<"{\"width\":"<<w<<",\"height\":"<<h<<",\"frames\":"<<frames<<",\"warmup\":"<<warmup<<",\"preparation_frames\":"<<preparation_frames<<",\"history_epoch\":\"reset-after-candidate-ready-v1\",\"mode\":\""<<std::string(mode.begin(),mode.end())<<"\",\"input\":\"synthetic-static-fp16\",\"timing_boundary\":\"Feature18-only\",\"game\":false,\"sr\":false}\n";
    std::cout<<"completed=true game=false sr=false frames="<<frames<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"host_failed="<<e.what()<<'\n';std::cerr.flush();ExitProcess(2);}}
