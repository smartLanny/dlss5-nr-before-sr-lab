// Real Feature1 using the frozen production ABI and private SR state machine.
// This host owns its device/queue. No injection, game installation or fake NGX.
#ifndef NR_PRIVATE_SR_CORE
#error Supply the frozen final Core source path.
#endif
#include NR_PRIVATE_SR_CORE
#include <dxgi1_4.h>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <vector>

namespace sr_host {
unsigned checks=0;
void Require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
void Hr(HRESULT r,const char* message){if(FAILED(r))std::printf("%s HRESULT=%08lx\n",message,(unsigned long)r);Require(SUCCEEDED(r),message);}
void Ngx(unsigned r,const char* message){std::printf("%s=%08x\n",message,r);Require(r==1,message);}
struct Host {
    ID3D12Device* device=nullptr;ID3D12CommandQueue* queue=nullptr;
    ID3D12CommandAllocator* allocator=nullptr;ID3D12GraphicsCommandList* list=nullptr;
    ID3D12Fence* fence=nullptr;UINT64 value=0,recording=1;bool released=true;
    std::vector<ID3D12Resource*> owned;
    Host(){
        IDXGIFactory4* factory=nullptr;Hr(CreateDXGIFactory1(IID_IDXGIFactory4,(void**)&factory),"factory");
        for(unsigned i=0;!device;++i){IDXGIAdapter1* adapter=nullptr;if(IDXGIFactory4_EnumAdapters1(factory,i,&adapter)==DXGI_ERROR_NOT_FOUND)break;
            DXGI_ADAPTER_DESC1 desc{};Hr(IDXGIAdapter1_GetDesc1(adapter,&desc),"adapter description");
            if(desc.VendorId==0x10de&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))
                Hr(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_12_0,IID_ID3D12Device,(void**)&device),"NVIDIA device");
            ReleaseCom(adapter);}
        ReleaseCom(factory);Require(device!=nullptr,"NVIDIA hardware required");
        D3D12_COMMAND_QUEUE_DESC q{};Hr(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,(void**)&queue),"queue");
        Hr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)&allocator),"allocator");
        Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_ID3D12GraphicsCommandList,(void**)&list),"list");
        Hr(ID3D12GraphicsCommandList_Close(list),"initial close");
        Hr(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)&fence),"fence");
    }
    // Main catches failure using ExitProcess: uncertain vendor/GPU references
    // must not unwind and release their backing allocations.
    ~Host(){if(std::uncaught_exceptions()>0||!released)return;for(auto*& r:owned)ReleaseCom(r);ReleaseCom(list);ReleaseCom(allocator);ReleaseCom(fence);ReleaseCom(queue);ReleaseCom(device);}
    void Begin(){Require(value==0||ID3D12Fence_GetCompletedValue(fence)>=value,"prior completion");
        Hr(ID3D12CommandAllocator_Reset(allocator),"allocator reset");Hr(ID3D12GraphicsCommandList_Reset(list,allocator,nullptr),"list reset");++recording;released=false;}
    nr_private_sr::Recording Recording(){return {1,1,(UINT64)list,recording,recording};}
    nr_private_sr::Fence Submit(){Hr(ID3D12GraphicsCommandList_Close(list),"close");ID3D12CommandList* lists[]={(ID3D12CommandList*)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue,1,lists);Hr(ID3D12CommandQueue_Signal(queue,fence,++value),"signal");return {1,(UINT64)fence,value};}
    void Wait(){HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(e!=nullptr,"completion event");
        Hr(ID3D12Fence_SetEventOnCompletion(fence,value,e),"completion registration");
        const DWORD result=WaitForSingleObject(e,20000);Require(result==WAIT_OBJECT_0,"bounded completion; no unsafe cleanup");CloseHandle(e);
        const auto done=ID3D12Fence_GetCompletedValue(fence);Require(done!=UINT64_MAX&&done>=value,"exact fence completion");
        // Reset destroys replayability of this completed recording.
        Hr(ID3D12CommandAllocator_Reset(allocator),"retire allocator");Hr(ID3D12GraphicsCommandList_Reset(list,allocator,nullptr),"retire recording");
        Hr(ID3D12GraphicsCommandList_Close(list),"retire close");released=true;}
    ID3D12Resource* Buffer(UINT64 bytes,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES h{};h.Type=type;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource* r=nullptr;Hr(ID3D12Device_CreateCommittedResource(device,&h,D3D12_HEAP_FLAG_NONE,&d,
            type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&r),"buffer");owned.push_back(r);return r;}
    void Barrier(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};ID3D12GraphicsCommandList_ResourceBarrier(list,1,&b);}
    ID3D12Resource* Plane(unsigned w,unsigned h,DXGI_FORMAT format,bool output,float carrier=.5f){
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};
        d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=format;
        if(output)d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ID3D12Resource* r=nullptr;Hr(ID3D12Device_CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&r),"plane");owned.push_back(r);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;ID3D12Device_GetCopyableFootprints(device,&d,0,1,0,&footprint,nullptr,nullptr,&bytes);
        auto* upload=Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD);unsigned char* mapped=nullptr;Hr(ID3D12Resource_Map(upload,0,nullptr,(void**)&mapped),"upload map");std::memset(mapped,0,(size_t)bytes);
        const unsigned bpp=format==DXGI_FORMAT_R32_FLOAT?4:format==DXGI_FORMAT_R32G32B32A32_FLOAT?16:8;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto* pixel=mapped+y*footprint.Footprint.RowPitch+x*bpp;
            if(format==DXGI_FORMAT_R32_FLOAT){const float z=.5f;std::memcpy(pixel,&z,4);}
            if(format==DXGI_FORMAT_R16G16B16A16_FLOAT){const unsigned short v=carrier==.5f?0x3800:carrier==.625f+1.f/1024.f?0x3902:carrier==.625f-1.f/2048.f?0x38ff:carrier>.5f?0x3900:0x3600;unsigned short rgba[]={v,v,v,0x3c00};std::memcpy(pixel,rgba,8);}}
        ID3D12Resource_Unmap(upload,0,nullptr);Begin();D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=r;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=upload;b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=footprint;
        ID3D12GraphicsCommandList_CopyTextureRegion(list,&a,0,0,0,&b,nullptr);Barrier(r,D3D12_RESOURCE_STATE_COPY_DEST,output?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Submit();Wait();return r;
    }
    void Pixels(ID3D12Resource* r,float expected,const wchar_t* file,double tolerance=.04){const auto d=ID3D12Resource_GetDesc(r);D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
        ID3D12Device_GetCopyableFootprints(device,&d,0,1,0,&footprint,nullptr,nullptr,&bytes);auto* rb=Buffer(bytes,D3D12_HEAP_TYPE_READBACK);Begin();
        Barrier(r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=rb;a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;a.PlacedFootprint=footprint;b.pResource=r;b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        ID3D12GraphicsCommandList_CopyTextureRegion(list,&a,0,0,0,&b,nullptr);Barrier(r,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Submit();Wait();
        unsigned char* mapped=nullptr;Hr(ID3D12Resource_Map(rb,0,nullptr,(void**)&mapped),"readback map");double sum=0,max_error=0;unsigned count=0;
        FILE* out=_wfopen(file,L"wb");Require(out!=nullptr,"capture file");
        for(unsigned y=0;y<d.Height;++y){Require(std::fwrite(mapped+y*footprint.Footprint.RowPitch,8,(size_t)d.Width,out)==d.Width,"capture row");
            for(unsigned x=8;x+8<d.Width;++x)if(y>=8&&y+8<d.Height){const auto* p=(unsigned short*)(mapped+y*footprint.Footprint.RowPitch+x*8);
                for(unsigned c=0;c<3;++c){const unsigned e=(p[c]>>10)&31,m=p[c]&1023;Require(e!=31,"finite actual SR output");
                    const float f=(p[c]&0x8000?-1.f:1.f)*(e?std::ldexp(1.f+float(m)/1024,int(e)-15):std::ldexp(float(m),-24));
                    sum+=f;max_error=std::max(max_error,std::fabs(double(f)-expected));++count;}}}
        std::fclose(out);ID3D12Resource_Unmap(rb,0,nullptr);std::printf("readback mean=%.7f expected=%.7f max_error=%.7f samples=%u\n",sum/count,expected,max_error,count);
        Require(max_error<=tolerance,"actual SR/composition pixel expectation");
    }
};
void NeutralAndTinyComposition(Host& h,ID3D12Resource* actual_sr){
    using namespace nr_edit_dx12;
    auto* kernels=Create(h.device,1,1,nr_edit_binaries::ModelKernels(),IsSameRrDepthDevice);Require(kernels!=nullptr,"production compose kernels");
    SetRetirementGuard(kernels,[](void* u,nr_private_sr::Recording){return ((Host*)u)->released;},&h);
    auto* original=h.Plane(320,192,DXGI_FORMAT_R16G16B16A16_FLOAT,false,.625f);
    auto* encoded=h.Plane(320,192,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    auto* tail=h.Plane(320,192,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
    auto* clean=h.Plane(640,384,DXGI_FORMAT_R16G16B16A16_FLOAT,false,.5f);
    auto* final=h.Plane(640,384,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    const float edits[]={0,1.f/1024.f,-1.f/2048.f};
    const wchar_t* captures[]={L"composed-neutral.fp16",L"composed-tiny-positive.fp16",L"composed-tiny-negative.fp16"};
    for(unsigned test=0;test<3;++test){auto* edited=h.Plane(320,192,DXGI_FORMAT_R16G16B16A16_FLOAT,false,.625f+edits[test]);h.Begin();
        nr_deferred_dx12::Constants c{};c.render_size[0]=320;c.render_size[1]=192;c.output_size[0]=640;c.output_size[1]=384;c.frame_pair_valid=c.private_sr_succeeded=1;
        const auto read=[](ID3D12Resource* r,unsigned format){return View{r,format,0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,true};};
        const auto uav=[](ID3D12Resource* r,unsigned format){return View{r,format,0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true};};
        Work w{};w.kernel=Kernel::DeferredEncode;w.recording=h.Recording();w.width=320;w.height=192;w.constants=&c;w.constant_bytes=sizeof(c);
        w.reads[0]=read(edited,10);w.reads[1]=read(original,10);w.writes[0]=uav(encoded,10);w.writes[1]=uav(tail,2);
        Require(Record(kernels,h.list,w).recorded,"encode exact neutral or quantized tiny edit");
        w.kernel=Kernel::DeferredCompose;w.width=640;w.height=384;w.reads={};w.writes={};
        w.reads[2]=uav(actual_sr,10);w.reads[3]=uav(tail,2);w.reads[4]=read(clean,10);w.writes[2]=uav(final,10);
        Require(Record(kernels,h.list,w).recorded,"compose actual DLSS carrier");const auto fence=h.Submit();
        Require(Submitted(kernels,w.recording,h.fence,fence,true),"composition receipt");h.Wait();Require(Collect(kernels),"composition retirement");
        h.Pixels(final,.5f+edits[test],captures[test],0);
    }
    Require(DestroyAfterRetirement(kernels),"composition resources retired");
}
int Run(const wchar_t* bridge){Host h;HMODULE module=LoadLibraryW(bridge);Require(module!=nullptr,"verified official NGX bootstrap");
    auto bootstrap=(unsigned(*)(ID3D12Device*))GetProcAddress(module,"NRChain_Bootstrap");Require(bootstrap!=nullptr,"bootstrap export");Ngx(bootstrap(h.device),"official bootstrap");
    HMODULE ngx=GetModuleHandleW(L"_nvngx.dll");Require(ngx!=nullptr,"official loader discovered core");
    auto alloc=(nr_private_sr_core::Allocate)GetProcAddress(ngx,"NVSDK_NGX_D3D12_AllocateParameters");
    auto create=(nr_private_sr_core::Create)GetProcAddress(ngx,"NVSDK_NGX_D3D12_CreateFeature");
    auto release=(nr_private_sr_core::Release)GetProcAddress(ngx,"NVSDK_NGX_D3D12_ReleaseFeature");
    auto destroy=(nr_private_sr_core::Destroy)GetProcAddress(ngx,"NVSDK_NGX_D3D12_DestroyParameters");
    Require(alloc&&create&&release&&destroy,"real original exports");
    auto capabilities=(nr_private_sr_core::Allocate)GetProcAddress(ngx,"NVSDK_NGX_D3D12_GetCapabilityParameters");
    Require(capabilities!=nullptr,"official capabilities export");NgxParams* caps=nullptr;Ngx(capabilities(&caps),"query capabilities");
    int available=0,init=0;GetInt(caps,"SuperSampling.Available",&available);GetInt(caps,"SuperSampling.FeatureInitResult",&init);
    std::printf("SR available=%d featureInit=%08x\n",available,unsigned(init));Ngx(destroy(caps),"destroy capabilities");
    NgxParams* params=nullptr;Ngx(alloc(&params),"allocate native parent");
    for(auto v:{std::pair{"Width",320u},{"Height",192u},{"OutWidth",640u},{"OutHeight",384u},{"CreationNodeMask",1u},{"VisibilityNodeMask",1u}})SetUInt(params,v.first,v.second);
    SetInt(params,"PerfQualityValue",0);SetInt(params,"DLSS.Feature.Create.Flags",1|2);
    NgxHandle* parent=nullptr;h.Begin();Ngx(create(h.list,1,params,&parent),"create native SR parent");Require(parent!=nullptr,"native parent handle");h.Submit();h.Wait();
    RememberFeature(parent,1,params);g_codec.device=h.device;
    nr_private_sr_core::Exports exports{};const auto observed=FindFeature(parent);
    Require(nr_private_sr_core::ResolveExports(exports,h.device,parent,observed.generation),"production private SR owner resolution");
    auto api=nr_private_sr_core::MakeApi(exports);api.recording_released=[](void* u,nr_private_sr::Recording){return ((Host*)u)->released;};api.retirement_user=&h;
    nr_private_sr::Shape shape{320,192,640,384,0,1|2,1,1,1};nr_private_sr::Context context;
    Require(context.Initialize(api,shape),"production private context");
    auto* depth=h.Plane(320,192,DXGI_FORMAT_R32_FLOAT,false);auto* motion=h.Plane(320,192,DXGI_FORMAT_R32G32_FLOAT,false);
    auto* output=h.Plane(640,384,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    nr_private_sr::Source previous{};unsigned sequence=0;
    for(unsigned test=0;test<3;++test){const float expected=test==0?.5f:test==1?.625f:.375f;auto* color=h.Plane(320,192,DXGI_FORMAT_R16G16B16A16_FLOAT,false,expected);
        for(unsigned step=0;step<4;++step){h.Begin();nr_private_sr::Frame f{};f.recording=h.Recording();f.source={1,1,++sequence};f.motion_reference=previous;
            f.color=color;f.depth=depth;f.motion=motion;f.output=output;f.mv_x=f.mv_y=1;f.reset=step==0;f.resource_contract_verified=true;
            const auto r=context.Record(h.list,f,shape);std::printf("source=%u status=%u result=%08x evaluated=%d\n",sequence,unsigned(r.status),r.result,r.evaluated);
            Require(r.recorded&&!context.Poisoned(),"actual private SR record");const auto fence=h.Submit();Require(context.Submitted(f.recording,fence,true),"exact SR submission");
            Require(!context.ReadyToRetire(),"recording remains pinned until explicit retirement");h.Wait();Require(context.Collect(),"collect completed SR");if(r.evaluated)previous=f.source;}
        const wchar_t* files[]={L"sr-neutral.fp16",L"sr-positive.fp16",L"sr-negative.fp16"};h.Pixels(output,expected,files[test]);
        if(test==0)NeutralAndTinyComposition(h,output);}
    Require(context.ReadyToRetire()&&context.Retire(),"private feature safe release");nr_private_sr_core::ReleaseExports(exports);
    ForgetFeature(parent);Ngx(release(parent),"release parent");Ngx(destroy(params),"destroy parent params");g_codec.device=nullptr;
    std::printf("PASS actual Feature1 private backend checks=%u; synthetic carrier, no NR/game/ghosting claim\n",checks);return 0;
}
}
int wmain(int argc,wchar_t** argv){if(argc!=2)return 2;try{return sr_host::Run(argv[1]);}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());std::fflush(nullptr);ExitProcess(1);}}
