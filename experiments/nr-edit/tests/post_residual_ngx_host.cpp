// Offline capture pixels -> production Encode -> real private Feature1 ->
// production Compose. Static plane guides are constructed here, not captured.
#define NR_PRIVATE_SR_HOST_LIBRARY
#include "private_sr_ngx_host.cpp"
#include "nr_post_residual_dx12.hpp"
namespace post_ngx {
using namespace sr_host;
struct InputHeader {
    char magic[8];unsigned version,width,height,nw,nh,kind;
    std::uint64_t stream,epoch,sequence;unsigned domain,reserved;
};
static_assert(sizeof(InputHeader)==64);
float Value(unsigned short h){const unsigned e=(h>>10)&31,m=h&1023;
    Require(e!=31,"finite FP16 input/output");return (h&0x8000?-1.f:1.f)*(e?std::ldexp(1.f+float(m)/1024,int(e)-15):std::ldexp(float(m),-24));}
unsigned short Half(float v){const float a=std::abs(v);const unsigned sign=std::signbit(v)?0x8000:0;
    if(a==0)return sign;int e=0;std::frexp(a,&e);--e;
    if(e< -14)return sign|unsigned(std::nearbyint(std::ldexp(a,24)));
    unsigned m=unsigned(std::nearbyint(std::ldexp(a,10-e)));if(m==2048){m=1024;++e;}
    return static_cast<unsigned short>(sign|unsigned(e+15)*1024|(m-1024));}
void Upload(Host& h,ID3D12Resource* image,const void* pixels,unsigned bpp,D3D12_RESOURCE_STATES state){
    const auto d=ID3D12Resource_GetDesc(image);D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
    ID3D12Device_GetCopyableFootprints(h.device,&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto* upload=h.Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD);
    unsigned char* dst=nullptr;Hr(ID3D12Resource_Map(upload,0,nullptr,(void**)&dst),"pair upload map");
    for(unsigned y=0;y<d.Height;++y)std::memcpy(dst+y*fp.Footprint.RowPitch,(const char*)pixels+y*d.Width*bpp,d.Width*bpp);
    ID3D12Resource_Unmap(upload,0,nullptr);h.Begin();h.Barrier(image,state,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
    to.pResource=image;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;ID3D12GraphicsCommandList_CopyTextureRegion(h.list,&to,0,0,0,&from,nullptr);
    h.Barrier(image,D3D12_RESOURCE_STATE_COPY_DEST,state);h.Submit();h.Wait();
}
std::vector<unsigned char> Capture(Host& h,ID3D12Resource* image,unsigned bpp,D3D12_RESOURCE_STATES state,const char* name){
    const auto d=ID3D12Resource_GetDesc(image);D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
    ID3D12Device_GetCopyableFootprints(h.device,&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto* rb=h.Buffer(bytes,D3D12_HEAP_TYPE_READBACK);
    h.Begin();h.Barrier(image,state,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION from{},to{};
    from.pResource=image;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=rb;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(h.list,&to,0,0,0,&from,nullptr);h.Barrier(image,D3D12_RESOURCE_STATE_COPY_SOURCE,state);h.Submit();h.Wait();
    std::vector<unsigned char> output(d.Width*d.Height*bpp);unsigned char* mapped=nullptr;Hr(ID3D12Resource_Map(rb,0,nullptr,(void**)&mapped),"transport readback");
    for(unsigned y=0;y<d.Height;++y)std::memcpy(output.data()+y*d.Width*bpp,mapped+y*fp.Footprint.RowPitch,d.Width*bpp);
    ID3D12Resource_Unmap(rb,0,nullptr);FILE* file=std::fopen(name,"wb");Require(file!=nullptr,"create capture");
    Require(std::fwrite(output.data(),1,output.size(),file)==output.size(),"complete raw capture");std::fclose(file);return output;
}
int Run(const wchar_t* bridge,const wchar_t* package){
    FILE* input=_wfopen(package,L"rb");Require(input!=nullptr,"prepared pair file");InputHeader p{};
    Require(std::fread(&p,1,sizeof(p),input)==sizeof(p)&&!std::memcmp(p.magic,"NRPOST1\0",8)&&p.version==1&&p.domain==1,"prepared pair header");
    Require(p.width&&p.height&&p.width<=16384&&p.height<=16384&&p.nw&&p.nh&&p.nw<=p.width&&p.nh<=p.height&&
        p.nw*4ull>=p.width&&p.nh*4ull>=p.height&&p.stream&&p.epoch&&p.sequence,"strict prepared dimensions/identity");
    std::vector<unsigned short> base(std::uint64_t(p.width)*p.height*4),candidate(base.size());
    Require(std::fread(base.data(),2,base.size(),input)==base.size()&&std::fread(candidate.data(),2,candidate.size(),input)==candidate.size()&&std::fgetc(input)==EOF,"complete pair payload");std::fclose(input);
    for(const auto* values:{&base,&candidate})for(auto v:*values){const float x=Value(v);Require(x>=0&&x<=1,"input outside declared SDR encoding; no clipping");}
    Host h;HMODULE module=LoadLibraryW(bridge);Require(module!=nullptr,"official bridge");
    auto bootstrap=(unsigned(*)(ID3D12Device*))GetProcAddress(module,"NRChain_Bootstrap");Require(bootstrap!=nullptr,"bootstrap export");Ngx(bootstrap(h.device),"official bootstrap");
    HMODULE ngx=GetModuleHandleW(L"_nvngx.dll");Require(ngx!=nullptr,"official NGX core");
    auto alloc=(nr_private_sr_core::Allocate)GetProcAddress(ngx,"NVSDK_NGX_D3D12_AllocateParameters");
    auto create=(nr_private_sr_core::Create)GetProcAddress(ngx,"NVSDK_NGX_D3D12_CreateFeature");
    auto release=(nr_private_sr_core::Release)GetProcAddress(ngx,"NVSDK_NGX_D3D12_ReleaseFeature");
    auto destroy=(nr_private_sr_core::Destroy)GetProcAddress(ngx,"NVSDK_NGX_D3D12_DestroyParameters");Require(alloc&&create&&release&&destroy,"actual exports");
    NgxParams* parameters=nullptr;Ngx(alloc(&parameters),"allocate owner");
    for(auto v:{std::pair{"Width",p.nw},{"Height",p.nh},{"OutWidth",p.width},{"OutHeight",p.height},{"CreationNodeMask",1u},{"VisibilityNodeMask",1u}})SetUInt(parameters,v.first,v.second);
    // SDR, working-grid MV, non-jittered, non-inverted, unity exposure.
    SetInt(parameters,"PerfQualityValue",0);SetInt(parameters,"DLSS.Feature.Create.Flags",2);
    NgxHandle* parent=nullptr;h.Begin();Ngx(create(h.list,1,parameters,&parent),"create real SR owner");Require(parent!=nullptr,"owner handle");h.Submit();h.Wait();
    RememberFeature(parent,1,parameters);g_codec.device=h.device;nr_private_sr_core::Exports exports{};
    Require(nr_private_sr_core::ResolveExports(exports,h.device,parent,FindFeature(parent).generation),"verified owner adapter");
    auto api=nr_private_sr_core::MakeApi(exports);api.recording_released=[](void* u,nr_private_sr::Recording){return ((Host*)u)->released;};api.retirement_user=&h;
    nr_private_sr::Shape shape{p.nw,p.nh,p.width,p.height,0,2,1,1,1};nr_private_sr::Context sr;
    Require(sr.Initialize(api,shape),"private SR init");
    auto* b=h.Plane(p.width,p.height,DXGI_FORMAT_R16G16B16A16_FLOAT,true);Upload(h,b,base.data(),8,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto* z=h.Plane(p.width,p.height,DXGI_FORMAT_R16G16B16A16_FLOAT,true);Upload(h,z,candidate.data(),8,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto* carrier=h.Plane(p.nw,p.nh,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    auto* tail=h.Plane(p.nw,p.nh,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
    auto* reconstructed=h.Plane(p.width,p.height,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    auto* depth=h.Plane(p.nw,p.nh,DXGI_FORMAT_R32_FLOAT,false);
    auto* motion=h.Plane(p.nw,p.nh,DXGI_FORMAT_R32G32_FLOAT,false);
    auto* kernels=nr_edit_dx12::Create(h.device,1,1,nr_edit_binaries::PostKernels(),IsSameRrDepthDevice);Require(kernels!=nullptr,"actual post PSOs");
    nr_edit_dx12::SetRetirementGuard(kernels,[](void* u,nr_private_sr::Recording){return ((Host*)u)->released;},&h);
    nr_private_sr::Frame frame{};frame.source={p.stream,p.epoch,p.sequence};frame.color=carrier;frame.depth=depth;frame.motion=motion;frame.output=reconstructed;
    frame.mv_x=frame.mv_y=1;frame.reset=true;frame.resource_contract_verified=true;
    h.Begin();frame.recording=h.Recording();h.Barrier(carrier,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto created=sr.Record(h.list,frame,shape);Require(created.recorded&&!created.evaluated&&!sr.Poisoned(),"private Create only");
    h.Barrier(carrier,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto fence=h.Submit();Require(sr.Submitted(frame.recording,fence,true),"Create exact fence");h.Wait();Require(sr.Collect(),"Create completion and recording release");
    ID3D12QueryHeap* timestamps=nullptr;D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=6;
    Hr(ID3D12Device_CreateQueryHeap(h.device,&q,IID_ID3D12QueryHeap,(void**)&timestamps),"timestamp heap");auto* times=h.Buffer(48,D3D12_HEAP_TYPE_READBACK);UINT64 frequency=0;
    Hr(ID3D12CommandQueue_GetTimestampFrequency(h.queue,&frequency),"timestamp frequency");
    const auto view=[](ID3D12Resource* resource,unsigned format){return nr_edit_dx12::View{resource,format,0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true};};
    h.Begin();frame.recording=h.Recording();nr_post_residual_dx12::Pair pair{};pair.base=view(b,10);pair.candidate=view(z,10);
    pair.base_rect=pair.candidate_rect={0,0,p.width,p.height};pair.base_source=pair.candidate_source=frame.source;
    pair.domain=nr_post_residual_dx12::Domain::SdrEncodedFp16;pair.same_encoding_verified=true;pair.work_width=p.nw;pair.work_height=p.nh;
    ID3D12GraphicsCommandList_EndQuery(h.list,timestamps,D3D12_QUERY_TYPE_TIMESTAMP,0);
    auto encoded=nr_post_residual_dx12::Encode(kernels,h.list,h.Recording(),pair,view(carrier,10),view(tail,2));Require(bool(encoded),"production post Encode");
    ID3D12GraphicsCommandList_EndQuery(h.list,timestamps,D3D12_QUERY_TYPE_TIMESTAMP,1);
    h.Barrier(carrier,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12GraphicsCommandList_EndQuery(h.list,timestamps,D3D12_QUERY_TYPE_TIMESTAMP,2);
    auto evaluated=sr.Record(h.list,frame,shape);Require(evaluated.evaluated&&!sr.Poisoned(),"one actual private SR Evaluate");
    ID3D12GraphicsCommandList_EndQuery(h.list,timestamps,D3D12_QUERY_TYPE_TIMESTAMP,3);
    D3D12_RESOURCE_BARRIER ready{};ready.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;ready.UAV.pResource=reconstructed;ID3D12GraphicsCommandList_ResourceBarrier(h.list,1,&ready);
    h.Barrier(carrier,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    nr_post_residual_dx12::Reconstruction result{};result.carrier=view(reconstructed,10);result.target=view(z,10);result.target_rect={0,0,p.width,p.height};
    result.recording=h.Recording();result.source=frame.source;result.encoded_token=encoded.dispatch.token;result.succeeded=result.target_contains_candidate=true;
    ID3D12GraphicsCommandList_EndQuery(h.list,timestamps,D3D12_QUERY_TYPE_TIMESTAMP,4);
    Require(nr_post_residual_dx12::Compose(kernels,h.list,encoded,result).recorded,"production post Compose");
    ID3D12GraphicsCommandList_EndQuery(h.list,timestamps,D3D12_QUERY_TYPE_TIMESTAMP,5);
    ID3D12GraphicsCommandList_ResolveQueryData(h.list,timestamps,D3D12_QUERY_TYPE_TIMESTAMP,0,6,times,0);
    fence=h.Submit();Require(sr.Submitted(frame.recording,fence,true)&&nr_edit_dx12::Submitted(kernels,frame.recording,h.fence,fence,true),"shared actual final fence");
    Require(!sr.ReadyToRetire(),"resources pinned before recording release");h.Wait();Require(sr.Collect()&&nr_edit_dx12::Collect(kernels),"exact final retirement");
    UINT64* ticks=nullptr;Hr(ID3D12Resource_Map(times,0,nullptr,(void**)&ticks),"GPU timing readback");double milliseconds[3]{};
    for(unsigned i=0;i<3;++i){Require(ticks[2*i+1]>=ticks[2*i],"monotonic timestamps");milliseconds[i]=double(ticks[2*i+1]-ticks[2*i])*1000/frequency;}ID3D12Resource_Unmap(times,0,nullptr);
    const auto stored=Capture(h,carrier,8,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,"carrier.fp16");
    const auto remainder=Capture(h,tail,16,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,"tail.fp32");
    const auto actual=Capture(h,reconstructed,8,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,"sr-output.fp16");
    const auto final=Capture(h,z,8,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,"final.fp16");
    const auto after=Capture(h,b,8,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,"base-after.fp16");
    Capture(h,depth,4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,"artificial-depth.fp32");
    Capture(h,motion,8,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,"artificial-motion.fp32");
    Require(after.size()==base.size()*2&&!std::memcmp(after.data(),base.data(),after.size()),"B never overwritten");
    const auto* sr_pixels=(const unsigned short*)actual.data();const auto* output=(const unsigned short*)final.data();const auto* tails=(const float*)remainder.data();
    unsigned changed=0,outside=0,carrier_outside=0;double max_error=0,delta_to_candidate=0;
    for(unsigned y=0;y<p.height;++y)for(unsigned x=0;x<p.width;++x){
        const unsigned ax=(2*x+1)*p.nw+p.width,ay=(2*y+1)*p.nh+p.height,dx=2*p.width,dy=2*p.height;
        const int ix=int(ax/dx)-1,iy=int(ay/dy)-1;const float fx=float(ax%dx)/dx,fy=float(ay%dy)/dy;
        float tail_rgb[3]{},weight=0;bool support=false;
        for(int j=0;j<2;++j)for(int i=0;i<2;++i){const unsigned sx=std::clamp(ix+i,0,int(p.nw)-1),sy=std::clamp(iy+j,0,int(p.nh)-1);
            const float w=(i?fx:1-fx)*(j?fy:1-fy);const auto* t=tails+4*(sy*p.nw+sx);Require(t[3]>=.5f,"finite pair produced valid tail");
            if(w>0)support|=t[3]>=1.5f;for(unsigned c=0;c<3;++c)tail_rgb[c]+=t[c]*w;weight+=w;}
        for(unsigned c=0;c<4;++c){const auto index=4*(y*p.width+x)+c;const float bv=Value(base[index]),ov=Value(output[index]);
            if(c==3){Require(output[index]==base[index],"B alpha preserved");continue;}
            const float rv=Value(sr_pixels[index]);carrier_outside+=rv<0||rv>1;
            const float expected=Value(Half(bv+(support?(2*std::clamp(rv,0.f,1.f)-1)*4:0)+tail_rgb[c]/weight));
            max_error=std::max(max_error,std::abs(double(ov)-expected));outside+=ov<0||ov>1;changed+=output[index]!=base[index];
            delta_to_candidate=std::max(delta_to_candidate,std::abs(double(ov)-Value(candidate[index])));
        }
    }
    Require(max_error<=.0009765625,"actual SR output composed once around B within two FP16 steps");
    if(p.kind)Require(!std::memcmp(final.data(),candidate.data(),final.size()),"zero/tiny control exact FP16 output with real SR");
    if(!p.kind)Require(changed!=0,"actual NR residual reached final output");
    D3D12_RESOURCE_DESC surfaces[]={ID3D12Resource_GetDesc(carrier),ID3D12Resource_GetDesc(tail),ID3D12Resource_GetDesc(reconstructed)};
    const auto allocation=ID3D12Device_GetResourceAllocationInfo(h.device,0,3,surfaces);
    FILE* metrics=std::fopen("metrics.json","wb");Require(metrics!=nullptr,"metrics file");
    std::fprintf(metrics,"{\"real_private_sr_evaluations\":1,\"shape\":[%u,%u,%u,%u],\"flags\":2,\"quality\":0,\"reset\":true,\"artificial_guides\":true,\"base_unchanged\":true,\"changed_channels\":%u,\"outside_sdr_channels\":%u,\"outside_carrier_channels\":%u,\"composition_max_error\":%.9g,\"max_change_from_candidate\":%.9g,\"extract_ms\":%.9g,\"sr_ms\":%.9g,\"compose_ms\":%.9g,\"transport_surface_bytes\":%llu,\"timing_scope\":\"one reset Evaluate; excludes NR, Create, uploads, captures and driver-private allocations; not game performance\"}\n",p.nw,p.nh,p.width,p.height,changed,outside,carrier_outside,max_error,delta_to_candidate,milliseconds[0],milliseconds[1],milliseconds[2],(unsigned long long)allocation.SizeInBytes);std::fclose(metrics);
    Require(sr.ReadyToRetire()&&sr.Retire()&&nr_edit_dx12::DestroyAfterRetirement(kernels),"exact safe release");ReleaseCom(timestamps);
    nr_private_sr_core::ReleaseExports(exports);ForgetFeature(parent);Ngx(release(parent),"release parent");Ngx(destroy(parameters),"destroy owner params");g_codec.device=nullptr;
    std::printf("ACTUAL_POST_SR checks=%u changed=%u outside_sdr=%u carrier_outside=%u GPU_ms=%.4f/%.4f/%.4f; no captured native guides or game claim\n",checks,changed,outside,carrier_outside,milliseconds[0],milliseconds[1],milliseconds[2]);
    return outside||carrier_outside?4:0; // A numerical finding is not an admitted SDR result.
}
}
int wmain(int argc,wchar_t** argv){if(argc!=3)return 2;try{return post_ngx::Run(argv[1],argv[2]);}
catch(const std::exception& e){std::fprintf(stderr,"FAIL POST_NGX %s\n",e.what());std::fflush(nullptr);ExitProcess(1);}}
