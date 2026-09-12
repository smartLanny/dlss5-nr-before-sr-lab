// Independent, bounded D3D12 NR experiment. No injection, game hooks or live
// runtime callback installation. This process owns its feature, queue and data.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "../src/nr_replay_format.h"
namespace fs=std::filesystem;
using Result=std::uint32_t;
struct Params{void **vtable;};
template<class Fn>Fn Slot(Params *p,unsigned at){return reinterpret_cast<Fn>(p->vtable[at]);}
void SetF(Params *p,const char *name,float v){Slot<void(*)(Params*,const char*,float)>(p,6)(p,name,v);}
void SetI(Params *p,const char *name,int v){Slot<void(*)(Params*,const char*,int)>(p,3)(p,name,v);}
void SetU(Params *p,const char *name,unsigned v){Slot<void(*)(Params*,const char*,unsigned)>(p,4)(p,name,v);}
void SetR(Params *p,const char *name,ID3D12Resource *v){Slot<void(*)(Params*,const char*,ID3D12Resource*)>(p,1)(p,name,v);}
template<class T>struct Com {
    T *p=nullptr;~Com(){if(p)p->Release();}
    Com()=default;Com(const Com&)=delete;Com&operator=(const Com&)=delete;
    Com(Com&&v)noexcept:p(v.p){v.p=nullptr;}
    T *operator->()const{return p;}T **address(){return &p;}
};
void Require(bool value,const char *why){if(!value)throw std::runtime_error(why);}
void Hr(HRESULT value,const char *why){if(FAILED(value)){std::fprintf(stderr,"%s HRESULT=0x%08X\n",why,static_cast<unsigned>(value));throw std::runtime_error(why);}}
void Ngx(Result value,const char *why){std::printf("%s=0x%08X\n",why,value);std::fflush(stdout);Require(value==1,why);}
struct Device {
    Com<ID3D12Device> device;Com<ID3D12CommandQueue> queue;
    Com<ID3D12CommandAllocator> allocator;Com<ID3D12GraphicsCommandList> list;Com<ID3D12Fence> fence;
    HANDLE event=nullptr;std::uint64_t serial=0;
    Device() {
        Com<IDXGIFactory6> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(factory.address())),"factory");
        bool found=false;
        for(unsigned i=0;;++i) {
            Com<IDXGIAdapter1> adapter;
            if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(adapter.address()))==DXGI_ERROR_NOT_FOUND)break;
            DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);
            if(desc.VendorId!=0x10de || (desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))continue;
            Hr(D3D12CreateDevice(adapter.p,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(device.address())),"NVIDIA device");
            std::printf("adapterVendor=%u adapterDevice=%u dedicatedBytes=%llu\n",desc.VendorId,desc.DeviceId,static_cast<unsigned long long>(desc.DedicatedVideoMemory));
            found=true;break;
        }
        Require(found,"no NVIDIA adapter");
        D3D12_COMMAND_QUEUE_DESC desc{};Hr(device->CreateCommandQueue(&desc,IID_PPV_ARGS(queue.address())),"queue");
        Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(allocator.address())),"allocator");
        Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.p,nullptr,IID_PPV_ARGS(list.address())),"list");
        Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(fence.address())),"fence");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"event");
    }
    ~Device(){if(event)CloseHandle(event);}
    void Execute() {
        Hr(list->Close(),"close");ID3D12CommandList *lists[]={list.p};queue->ExecuteCommandLists(1,lists);
        Hr(queue->Signal(fence.p,++serial),"signal");Hr(fence->SetEventOnCompletion(serial,event),"completion event");
        Require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"offline GPU completion timeout");
        Require(fence->GetCompletedValue()!=UINT64_MAX,"device removed");
        Hr(allocator->Reset(),"allocator reset");Hr(list->Reset(allocator.p,nullptr),"list reset");
    }
    void Barrier(ID3D12Resource *r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        if(before==after)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
    }
    Com<ID3D12Resource> Texture(unsigned width,unsigned height,DXGI_FORMAT format,bool output) {
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;
        d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Format=format;
        d.Flags=output?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Com<ID3D12Resource> result;Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,
            output?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(result.address())),"texture");return result;
    }
    Com<ID3D12Resource> Buffer(std::uint64_t bytes,D3D12_HEAP_TYPE type) {
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;
        d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;Com<ID3D12Resource> result;
        Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,
            type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,IID_PPV_ARGS(result.address())),"buffer");return result;
    }
    void Upload(ID3D12Resource *texture,const std::vector<unsigned char> &data,unsigned pixel_bytes) {
        auto d=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
        device->GetCopyableFootprints(&d,0,1,0,&footprint,nullptr,nullptr,&bytes);
        Require(data.size()==d.Width*d.Height*pixel_bytes,"upload shape mismatch");
        auto upload=Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD);void *mapped=nullptr;D3D12_RANGE none{};
        Hr(upload->Map(0,&none,&mapped),"map upload");std::memset(mapped,0,static_cast<std::size_t>(bytes));
        for(unsigned y=0;y<d.Height;++y)std::memcpy(static_cast<unsigned char*>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch,
            data.data()+y*d.Width*pixel_bytes,static_cast<std::size_t>(d.Width*pixel_bytes));
        upload->Unmap(0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=texture;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource=upload.p;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        Barrier(texture,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Execute();
    }
    std::vector<unsigned char> Read(ID3D12Resource *texture) {
        const auto d=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
        device->GetCopyableFootprints(&d,0,1,0,&footprint,nullptr,nullptr,&bytes);auto readback=Buffer(bytes,D3D12_HEAP_TYPE_READBACK);
        Barrier(texture,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=readback.p;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
        src.pResource=texture;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        Barrier(texture,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Execute();
        void *mapped=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};Hr(readback->Map(0,&range,&mapped),"map NR result");
        std::vector<unsigned char> result(static_cast<std::size_t>(d.Width*d.Height*8));
        for(unsigned y=0;y<d.Height;++y)std::memcpy(result.data()+y*d.Width*8,
            static_cast<unsigned char*>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch,static_cast<std::size_t>(d.Width*8));
        D3D12_RANGE none{};readback->Unmap(0,&none);return result;
    }
};
struct RuntimeRecord {std::uint32_t version,type,id,present;std::uint64_t value;};
static_assert(sizeof(RuntimeRecord)==24);
unsigned callback_calls=0;
bool runtime_structure_enabled=false,runtime_abi_rejected=false;
double runtime_structure=1;
bool TraceRuntime(unsigned feature,unsigned count,void *opaque) {
    ++callback_calls;
    if(feature!=18 || !opaque || count!=8){runtime_abi_rejected=true;std::printf("runtimeCallback=unexpected feature=%u count=%u\n",feature,count);return false;}
    auto *mutable_records=static_cast<RuntimeRecord*>(opaque);
    unsigned ids=0;
    for(unsigned i=0;i<count;++i) {
        const auto &r=mutable_records[i];
        if(r.version!=1 || r.type!=2 || r.id>=8 || (ids&(1u<<r.id))){runtime_abi_rejected=true;return false;}
        ids|=1u<<r.id;
    }
    if(callback_calls<=4) {
        const auto *records=static_cast<const RuntimeRecord*>(opaque);
        for(unsigned i=0;i<count;++i)std::printf("runtimeCallback call=%u id=%u version=%u type=%u present=%u bits=%016llx\n",
            callback_calls,records[i].id,records[i].version,records[i].type,records[i].present,static_cast<unsigned long long>(records[i].value));
        std::fflush(stdout);
    }
    if(runtime_structure_enabled) {
        for(unsigned i=0;i<count;++i)if(mutable_records[i].id==2) {
            mutable_records[i].present=1;
            std::memcpy(&mutable_records[i].value,&runtime_structure,sizeof(runtime_structure));
        }
        return true; // Explicit isolated ID-2 experiment; never used by the addon.
    }
    return false;
}
struct Runtime {
    HMODULE core=nullptr,nr=nullptr,bridge=nullptr;
    void *create=nullptr,*evaluate=nullptr,*release=nullptr;
    Result(*allocate)(Params**)=nullptr;Result(*destroy)(Params*)=nullptr;
    Result(*call_create)(void*,ID3D12GraphicsCommandList*,int,void*,void**)=nullptr;
    Result(*call_evaluate)(void*,ID3D12GraphicsCommandList*,const void*,const void*,void*)=nullptr;
    Result(*call_release)(void*,void*)=nullptr;
    Result(*shutdown)(ID3D12Device*)=nullptr;
    Runtime(Device &gpu,const fs::path &directory,const fs::path &core_path,bool observed,bool trace,const fs::path &data) {
        Require(fs::is_regular_file(core_path) && fs::is_regular_file(directory/L"nvngx_dlssnr.dll") &&
            fs::is_regular_file(directory/L"nrchain_nvngx.dll"),"missing explicit runtime files");
        core=LoadLibraryExW(core_path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        nr=LoadLibraryExW((directory/L"nvngx_dlssnr.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        bridge=LoadLibraryExW((directory/L"nrchain_nvngx.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        Require(core&&nr&&bridge,"runtime load failed");
        auto init=reinterpret_cast<Result(*)(unsigned long long,const wchar_t*,ID3D12Device*,unsigned)>(GetProcAddress(core,"NVSDK_NGX_D3D12_Init"));
        auto attach=reinterpret_cast<Result(*)(void*,unsigned long long,const wchar_t*,ID3D12Device*,unsigned,const void*)>(GetProcAddress(bridge,"NRChain_Init"));
        void *target=reinterpret_cast<void*>(GetProcAddress(nr,"NVSDK_NGX_D3D12_Init_Ext"));
        allocate=reinterpret_cast<decltype(allocate)>(GetProcAddress(core,"NVSDK_NGX_D3D12_AllocateParameters"));
        destroy=reinterpret_cast<decltype(destroy)>(GetProcAddress(core,"NVSDK_NGX_D3D12_DestroyParameters"));
        create=reinterpret_cast<void*>(GetProcAddress(nr,"NVSDK_NGX_D3D12_CreateFeature"));
        evaluate=reinterpret_cast<void*>(GetProcAddress(nr,"NVSDK_NGX_D3D12_EvaluateFeature"));
        release=reinterpret_cast<void*>(GetProcAddress(nr,"NVSDK_NGX_D3D12_ReleaseFeature"));
        call_create=reinterpret_cast<decltype(call_create)>(GetProcAddress(bridge,"NRChain_Create"));
        call_evaluate=reinterpret_cast<decltype(call_evaluate)>(GetProcAddress(bridge,"NRChain_Evaluate"));
        call_release=reinterpret_cast<decltype(call_release)>(GetProcAddress(bridge,"NRChain_Release"));
        shutdown=reinterpret_cast<decltype(shutdown)>(GetProcAddress(nr,"NVSDK_NGX_D3D12_Shutdown1"));
        Require(init&&attach&&target&&allocate&&destroy&&create&&evaluate&&release&&call_create&&call_evaluate&&call_release,"runtime exports incomplete");
        const unsigned long long app=observed?0x876232Cull:0xE658703ull;const unsigned sdk=observed?21:19;
        std::printf("initConvention=%s appId=%llx sdk=%u\n",observed?"independent-observed":"core-current",app,sdk);
        Ngx(init(app,data.c_str(),gpu.device.p,sdk),"core init");
        const auto path=observed?directory/L"nvngx_dlssnr.dll":directory;
        Ngx(attach(target,app,path.c_str(),gpu.device.p,sdk,nullptr),"snippet init");
        if(trace) {
            auto set=reinterpret_cast<Result(*)(bool(*)(unsigned,unsigned,void*))>(GetProcAddress(nr,"NVSDK_NGX_SetRuntimeParamsCallback"));
            Require(set!=nullptr,"runtime callback export unavailable");Ngx(set(TraceRuntime),"isolated callback registration");
        }
    }
    ~Runtime() {
        // Process isolation is intentional: uncertain/failed native work is not
        // force-released. Successful features are explicitly released below.
    }
};
struct Image {unsigned width=0,height=0,pixel_bytes=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;std::vector<unsigned char> bytes;};
Image PlaneImage(const nr_replay::Header &header,const std::vector<unsigned char>&payload,nr_replay::Role role) {
    const auto &p=header.planes[static_cast<unsigned>(role)];
    Require(p.bytes && p.x==0 && p.y==0 && p.width==p.resource_width && p.height==p.resource_height,
        "model guide must be complete mip-0 resource, not a cropped screenshot");
    Image image;image.width=p.width;image.height=p.height;
    image.pixel_bytes=p.channels*(p.encoding==2?2:4);image.format=static_cast<DXGI_FORMAT>(p.view_format);
    const bool native=(image.format==DXGI_FORMAT_R16G16B16A16_FLOAT&&p.channels==4&&p.encoding==2)||
        (image.format==DXGI_FORMAT_R16G16_FLOAT&&p.channels==2&&p.encoding==2)||
        (image.format==DXGI_FORMAT_R32_FLOAT&&p.channels==1&&p.encoding==1)||
        (image.format==DXGI_FORMAT_R32G32_FLOAT&&p.channels==2&&p.encoding==1);
    Require(native,"this replay tool does not yet support the captured guide storage; no inferred conversion");
    image.bytes.assign(payload.begin()+p.offset,payload.begin()+p.offset+std::uint64_t(p.width)*p.height*image.pixel_bytes);return image;
}
void Apply(Params *params,const nr_replay::Header &header) {
    for(unsigned i=0;i<header.parameter_count;++i) {
        const auto &p=header.parameters[i];if(!p.present)continue;
        if(p.type==1)SetU(params,p.name,p.value);
        else if(p.type==2){int v=0;std::memcpy(&v,&p.value,4);SetI(params,p.name,v);}
        else {float v=0;std::memcpy(&v,&p.value,4);SetF(params,p.name,v);}
    }
}
void SyntheticParams(Params *p,unsigned w,unsigned h,float structure,float skin,int mask,int flags) {
    for(const char *key:{"Width","OutWidth","DLSSNR.Width","DLSSNR.InputWidth","DLSSNR.OutputWidth","DLSSNR.Output.Width"})SetU(p,key,w);
    for(const char *key:{"Height","OutHeight","DLSSNR.Height","DLSSNR.InputHeight","DLSSNR.OutputHeight","DLSSNR.Output.Height"})SetU(p,key,h);
    for(const char *name:{"Color","Depth","MVec","Output"}) {
        const std::string prefix=std::string("DLSSNR.")+name+"Subrect";
        SetU(p,(prefix+"BaseX").c_str(),0);SetU(p,(prefix+"BaseY").c_str(),0);
        SetU(p,(prefix+"Width").c_str(),w);SetU(p,(prefix+"Height").c_str(),h);
    }
    SetI(p,"DLSS.Feature.Create.Flags",flags);SetI(p,"PerfQualityValue",5);
    SetI(p,"DLSSNR.Enabled",1);SetI(p,"DLSSNR.DepthInverted",0);
    SetF(p,"DLSSNR.Intensity",1.2f);SetF(p,"DLSSNR.LocalToneStrength",1);
    SetF(p,"DLSSNR.LocalStructureStrength",structure);SetF(p,"DLSSNR.SkinStructureStrength",skin);
    SetI(p,"DLSSNR.UseAutoMask",mask);SetU(p,"DLSSNR.Style",0);SetU(p,"DLSSNR.UICorrection",1);
    SetF(p,"DLSSNR.ScalingRatio",1);SetF(p,"DLSSNR.Scale",1);SetI(p,"DLSSNR.Upscaling",0);
    SetF(p,"DLSSNR.MVecScaleX",1);SetF(p,"DLSSNR.MVecScaleY",1);
    SetF(p,"DLSS.Pre.Exposure",1);SetF(p,"DLSS.Exposure.Scale",1);
    SetR(p,"ExposureTexture",nullptr);
}
std::vector<unsigned char> ReadBytes(const fs::path &path,std::uint64_t expected) {
    Require(expected<=nr_replay::kFrameLimit && fs::file_size(path)==expected,"input length mismatch");
    std::ifstream file(path,std::ios::binary);std::vector<unsigned char> bytes(static_cast<std::size_t>(expected));
    file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));Require(bool(file),"input read failed");return bytes;
}
int wmain(int argc,wchar_t **argv) {
    try {
        fs::path runtime,core,sequence,proxy,output;unsigned width=0,height=0,frames=16;float structure=1,skin=0;
        int mask=1,flags=0;bool trace=false,observed=false,structure_override=false,skin_override=false,all=false;
        std::wstring switch_mode;unsigned segment=32;
        for(int i=1;i<argc;++i) {
            const std::wstring arg=argv[i];
            auto value=[&](){Require(i+1<argc,"missing option value");return std::wstring(argv[++i]);};
            if(arg==L"--runtime")runtime=value();else if(arg==L"--core")core=value();
            else if(arg==L"--sequence")sequence=value();else if(arg==L"--proxy")proxy=value();
            else if(arg==L"--output")output=value();else if(arg==L"--width")width=std::stoul(value());
            else if(arg==L"--height")height=std::stoul(value());else if(arg==L"--frames")frames=std::stoul(value());
            else if(arg==L"--structure"){structure=std::stof(value());structure_override=true;}
            else if(arg==L"--skin"){skin=std::stof(value());skin_override=true;}
            else if(arg==L"--auto-mask")mask=std::stoi(value());else if(arg==L"--flags")flags=std::stoi(value());
            else if(arg==L"--runtime-structure"){runtime_structure=std::stod(value());runtime_structure_enabled=true;trace=true;}
            else if(arg==L"--switch-mode")switch_mode=value();else if(arg==L"--segment-frames")segment=std::stoul(value());
            else if(arg==L"--trace-runtime")trace=true;else if(arg==L"--observed-init")observed=true;
            else if(arg==L"--save-all")all=true;else throw std::runtime_error("unknown argument");
        }
        Require(!runtime.empty()&&!core.empty()&&!output.empty()&&(sequence.empty()!=proxy.empty()),
            "usage: --runtime DIR --core FILE --output NEW_DIR (--sequence DIR | --proxy FP16_RGBA --width N --height N) [--trace-runtime]");
        Require(std::isfinite(structure)&&structure>=0&&structure<=2&&std::isfinite(skin)&&skin>=-1&&skin<=2&&
            (mask==0||mask==1)&&frames>=1&&frames<=128,"invalid bounded model controls");
        Require(runtime.is_absolute()&&core.is_absolute(),"runtime and driver paths must be absolute");
        Require(!runtime_structure_enabled||(std::isfinite(runtime_structure)&&runtime_structure>=0&&runtime_structure<=2),"invalid runtime structure");
        Require(switch_mode.empty()||(!proxy.empty()&&(switch_mode==L"recreate"||switch_mode==L"runtime")&&segment>=4&&segment<=64),"invalid switch experiment");
        if(!switch_mode.empty()) {
            frames=segment*3;
            if(switch_mode==L"runtime"){trace=true;runtime_structure_enabled=true;runtime_structure=1;}
        }
        Require(!fs::exists(output)&&fs::create_directories(output),"output must be a new directory");
        Device gpu;Runtime api(gpu,runtime,core,observed,trace,output);
        Params *params=nullptr;Ngx(api.allocate(&params),"allocate parameters");Require(params!=nullptr,"null parameters");
        void *feature=nullptr;nr_replay::Sequence checked;
        Image repeated;
        if(!proxy.empty()) {
            Require(width&&height&&width<=4096&&height<=4096,"invalid synthetic extent");
            repeated={width,height,8,DXGI_FORMAT_R16G16B16A16_FLOAT,ReadBytes(proxy,std::uint64_t(width)*height*8)};
        } else frames=16;
        for(unsigned index=0;index<frames;++index) {
            const float segment_structure=!switch_mode.empty()&&index/segment==1?2.f:1.f;
            const bool recreate=switch_mode==L"recreate" && index && index%segment==0;
            if(recreate) {
                Ngx(api.call_release(api.release,feature),"switch release");feature=nullptr;
                Ngx(api.destroy(params),"switch destroy parameters");params=nullptr;
                Ngx(api.allocate(&params),"switch allocate parameters");Require(params!=nullptr,"null switch parameters");
            }
            if(switch_mode==L"runtime")runtime_structure=segment_structure;
            nr_replay::Header header{};Image color,depth,motion;
            if(!sequence.empty()) {
                wchar_t leaf[32]{};swprintf_s(leaf,L"frame-%02u.nrf",index);const auto path=sequence/leaf;
                std::ifstream file(path,std::ios::binary);file.read(reinterpret_cast<char*>(&header),sizeof(header));
                Require(bool(file)&&nr_replay::ValidHeader(header)&&fs::file_size(path)==sizeof(header)+header.payload_bytes,"invalid replay file");
                Require(checked.Accept(header),"replay history/identity mismatch");
                std::vector<unsigned char> payload(static_cast<std::size_t>(header.payload_bytes));
                file.read(reinterpret_cast<char*>(payload.data()),static_cast<std::streamsize>(payload.size()));Require(bool(file),"truncated replay");
                color=PlaneImage(header,payload,nr_replay::Role::Proxy);depth=PlaneImage(header,payload,nr_replay::Role::Depth);
                motion=PlaneImage(header,payload,nr_replay::Role::Motion);Apply(params,header);
                if(structure_override)SetF(params,"DLSSNR.LocalStructureStrength",structure);
                if(skin_override)SetF(params,"DLSSNR.SkinStructureStrength",skin);
            } else {
                color=repeated;
                depth={width,height,4,DXGI_FORMAT_R32_FLOAT,std::vector<unsigned char>(std::size_t(width)*height*4)};
                motion={width,height,4,DXGI_FORMAT_R16G16_FLOAT,std::vector<unsigned char>(std::size_t(width)*height*4)};
                SyntheticParams(params,width,height,switch_mode==L"recreate"?segment_structure:structure,skin,mask,flags);
                SetI(params,"DLSSNR.Reset",(!index||recreate)?1:0);
            }
            auto color_gpu=gpu.Texture(color.width,color.height,color.format,false);
            auto depth_gpu=gpu.Texture(depth.width,depth.height,depth.format,false);
            auto motion_gpu=gpu.Texture(motion.width,motion.height,motion.format,false);
            auto result=gpu.Texture(color.width,color.height,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
            gpu.Upload(color_gpu.p,color.bytes,color.pixel_bytes);gpu.Upload(depth_gpu.p,depth.bytes,depth.pixel_bytes);gpu.Upload(motion_gpu.p,motion.bytes,motion.pixel_bytes);
            SetR(params,"DLSSNR.Color",color_gpu.p);SetR(params,"DLSSNR.Depth",depth_gpu.p);SetR(params,"DLSSNR.MVec",motion_gpu.p);SetR(params,"DLSSNR.Output",result.p);
            if(!feature){Ngx(api.call_create(api.create,gpu.list.p,18,params,&feature),"create feature 18");Require(feature!=nullptr,"null NR feature");}
            Ngx(api.call_evaluate(api.evaluate,gpu.list.p,feature,params,nullptr),"evaluate feature 18");
            Require(!runtime_abi_rejected,"runtime callback ABI was not the locally verified layout");
            const auto data=gpu.Read(result.p);
            if(all||index+1==frames) {
                wchar_t leaf[32]{};swprintf_s(leaf,L"nr-%03u.rgba16",index);
                std::ofstream file(output/leaf,std::ios::binary);file.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));Require(bool(file),"NR export failed");
            }
            std::printf("completed frame=%u width=%u height=%u bytes=%zu\n",index,color.width,color.height,data.size());std::fflush(stdout);
            width=color.width;height=color.height;
        }
        Ngx(api.call_release(api.release,feature),"release feature 18");Ngx(api.destroy(params),"destroy parameters");
        std::ofstream report(output/L"result.json");
        report<<"{\n  \"schema\": \"nr-model-lab-v1\",\n  \"success\": true,\n  \"frames\": "<<frames
            <<",\n  \"width\": "<<width<<",\n  \"height\": "<<height<<",\n  \"runtime_callback_calls\": "<<callback_calls
            <<",\n  \"structure\": "<<structure<<",\n  \"skin\": "<<skin<<",\n  \"auto_mask\": "<<mask
            <<",\n  \"runtime_structure_override\": "<<(runtime_structure_enabled?"true":"false")
            <<",\n  \"switch_mode\": \""<<(switch_mode==L"runtime"?"runtime":switch_mode==L"recreate"?"recreate":"none")<<"\""
            <<",\n  \"input_kind\": \""<<(sequence.empty()?"proxy-with-synthetic-zero-guides":"complete-captured-sequence")
            <<"\",\n  \"scope\": \"Independent NVIDIA inference only; not in-game face-mask, final image or latency acceptance\"\n}\n";
        Require(bool(report),"report export failed");return 0;
    } catch(const std::exception &e){std::fprintf(stderr,"MODEL LAB FAILED: %s\n",e.what());return 1;}
}
