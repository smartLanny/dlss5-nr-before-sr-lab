// Exercises the actual post pipeline. Default API is an explicitly labelled
// fixture; --real uses the installed original NGX Feature1, never mocked inference.
#define NR_EDIT_SHADER_VALIDATION_LIBRARY
#include "windows_shader_validation.cpp"
#include "nr_post_private_sr_dx12.hpp"
#include <map>
#include <set>
struct ID3D11Resource;
using NVSDK_NGX_Result=unsigned;
#include "src/interop/nr_ngx_parameter_abi.h"

namespace post_sr_test {
namespace pipeline=nr_post_private_sr_dx12;
using Recording=nr_private_sr::Recording;
using Source=nr_private_sr::Source;
unsigned checks=0;
void Need(bool ok,const char* message) {++checks;if(!ok)throw std::runtime_error(message);}
struct Block {std::map<std::string,void*> resources;std::map<std::string,float> floats;std::map<std::string,int> ints;};
struct Rig {
    Harness* h=nullptr;
    ResourceOwner* owner=nullptr;
    pipeline::Context* context=nullptr;
    std::set<std::uint64_t> retired;
    std::uint64_t fence_value=0,sequence=0;
    unsigned creates=0,evaluates=0,releases=0,allocations=0,destroys=0,reset_count=0;
    unsigned width=0,height=0,nw=0,nh=0;
    ID3D12Resource* fixture_output=nullptr;
    nr_private_sr::Api api{};
    HMODULE bridge=nullptr,ngx=nullptr;
    using Allocate=unsigned(*)(NVSDK_NGX_Parameter**);
    using Destroy=unsigned(*)(NVSDK_NGX_Parameter*);
    using Create=unsigned(*)(ID3D12GraphicsCommandList*,int,NVSDK_NGX_Parameter*,void**);
    using Evaluate=unsigned(*)(ID3D12GraphicsCommandList*,const void*,const NVSDK_NGX_Parameter*,void*);
    using Release=unsigned(*)(void*);
    Allocate allocate=nullptr;Destroy destroy=nullptr;Create create=nullptr;Evaluate evaluate=nullptr;Release release=nullptr;
    bool real=false,hold_sr_completion=false;
    bool spatial_fixture=false;
    ID3D12RootSignature* spatial_root=nullptr;
    ID3D12PipelineState* spatial_pipeline=nullptr;
    bool dark_quality=false;
    ID3D12DescriptorHeap* spatial_heap=nullptr;
    std::map<void*,unsigned> retained_resources;
    std::array<std::vector<char>,2> shader_bytes;

    Rig(const std::filesystem::path& shaders,bool hardware,const wchar_t* real_bridge,bool quality)
        :width(real_bridge||quality?640:64),height(real_bridge||quality?384:48),
         nw(width/2),nh(height/2),real(real_bridge!=nullptr) {
        h=new Harness(shaders,hardware||real);owner=new ResourceOwner;
        api.user=this;api.original_sr_owner_verified=true;
        api.retirement_user=this;
        api.recording_released=[](void* u,Recording r){return static_cast<Rig*>(u)->retired.count(r.lifetime)!=0;};
        api.retain_resource=[](void* u,void* p){auto& r=*static_cast<Rig*>(u);++r.retained_resources[p];static_cast<IUnknown*>(p)->AddRef();};
        api.release_resource=[](void* u,void* p){auto& r=*static_cast<Rig*>(u);Need(r.retained_resources[p]>0,"resource release has a matching owned reference");--r.retained_resources[p];static_cast<IUnknown*>(p)->Release();};
        api.retain_fence=[](void*,std::uint64_t p){reinterpret_cast<ID3D12Fence*>(p)->AddRef();};
        api.release_fence=[](void*,std::uint64_t p){reinterpret_cast<ID3D12Fence*>(p)->Release();};
        api.poll=[](void* u,nr_private_sr::Fence f){auto& r=*static_cast<Rig*>(u);return nr_private_sr::Completion{f.queue,f.object,r.hold_sr_completion?0:reinterpret_cast<ID3D12Fence*>(f.object)->GetCompletedValue()};};
        if(real) {
            bridge=LoadLibraryW(real_bridge);Need(bridge!=nullptr,"load existing bootstrap");
            auto init=reinterpret_cast<unsigned(*)(ID3D12Device*)>(GetProcAddress(bridge,"NRChain_Bootstrap"));
            Need(init&&init(h->device)==1,"actual official bootstrap");ngx=GetModuleHandleW(L"_nvngx.dll");
            Need(ngx!=nullptr,"actual original loader module");
            allocate=reinterpret_cast<Allocate>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_AllocateParameters"));
            destroy=reinterpret_cast<Destroy>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_DestroyParameters"));
            create=reinterpret_cast<Create>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_CreateFeature"));
            evaluate=reinterpret_cast<Evaluate>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_EvaluateFeature"));
            release=reinterpret_cast<Release>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_ReleaseFeature"));
            Need(allocate&&destroy&&create&&evaluate&&release,"all actual Feature1 exports");
        }
        api.allocate=[](void* u,void** out){auto& r=*static_cast<Rig*>(u);++r.allocations;
            if(r.real){NVSDK_NGX_Parameter* p=nullptr;const auto result=r.allocate(&p);*out=p;return result;}
            *out=new Block;return 1u;};
        api.destroy=[](void* u,void* p){auto& r=*static_cast<Rig*>(u);++r.destroys;
            if(r.real)return r.destroy(static_cast<NVSDK_NGX_Parameter*>(p));delete static_cast<Block*>(p);return 1u;};
        api.set_u=[](void* u,void* p,const char* key,unsigned value){auto& r=*static_cast<Rig*>(u);
            if(r.real)static_cast<NVSDK_NGX_Parameter*>(p)->Set(key,value);else static_cast<Block*>(p)->ints[key]=int(value);};
        api.set_i=[](void* u,void* p,const char* key,int value){auto& r=*static_cast<Rig*>(u);
            if(r.real)static_cast<NVSDK_NGX_Parameter*>(p)->Set(key,value);else static_cast<Block*>(p)->ints[key]=value;};
        api.set_f=[](void* u,void* p,const char* key,float value){auto& r=*static_cast<Rig*>(u);
            if(r.real)static_cast<NVSDK_NGX_Parameter*>(p)->Set(key,value);else static_cast<Block*>(p)->floats[key]=value;};
        api.set_r=[](void* u,void* p,const char* key,void* value){auto& r=*static_cast<Rig*>(u);
            if(r.real)static_cast<NVSDK_NGX_Parameter*>(p)->Set(key,static_cast<ID3D12Resource*>(value));else static_cast<Block*>(p)->resources[key]=value;};
        api.create=[](void* u,void* list,int feature,void* params,void** handle){auto& r=*static_cast<Rig*>(u);++r.creates;
            Need(feature==1,"only private SR; not private RR");
            if(r.real)return r.create(static_cast<ID3D12GraphicsCommandList*>(list),feature,static_cast<NVSDK_NGX_Parameter*>(params),handle);
            *handle=&r;return 1u;};
        api.evaluate=[](void* u,void* list,void* handle,void* params){auto& r=*static_cast<Rig*>(u);++r.evaluates;
            if(r.real)return r.evaluate(static_cast<ID3D12GraphicsCommandList*>(list),handle,static_cast<NVSDK_NGX_Parameter*>(params),nullptr);
            auto& p=*static_cast<Block*>(params);r.reset_count+=p.ints.at("Reset")!=0;
            Need(p.floats.at("Jitter.Offset.X")==0&&p.floats.at("Jitter.Offset.Y")==0,"post source phase is not game render jitter");
            auto* output=static_cast<ID3D12Resource*>(p.resources.at("Output"));auto* l=static_cast<ID3D12GraphicsCommandList*>(list);
            if(r.spatial_fixture){r.RecordSpatial(static_cast<ID3D12Resource*>(p.resources.at("Color")),output,l);return 1u;}
            Need(r.fixture_output!=nullptr,"fixture output supplied explicitly");
            Transition(l,r.fixture_output,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
            Transition(l,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
            l->CopyResource(output,r.fixture_output);
            Transition(l,output,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Transition(l,r.fixture_output,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);return 1u;};
        api.release=[](void* u,void* handle){auto& r=*static_cast<Rig*>(u);++r.releases;return r.real?r.release(handle):1u;};
        std::array<nr_edit_dx12::Bytecode,static_cast<unsigned>(nr_edit_dx12::Kernel::Count)> code{};
        shader_bytes[0]=Read(shaders/"post_extract.cso");shader_bytes[1]=Read(shaders/"post_compose.cso");
        code[unsigned(nr_edit_dx12::Kernel::PostExtract)]={shader_bytes[0].data(),shader_bytes[0].size()};
        code[unsigned(nr_edit_dx12::Kernel::PostCompose)]={shader_bytes[1].data(),shader_bytes[1].size()};
        nr_private_sr::Shape shape{nw,nh,width,height,0,3,1,1,1};std::uint64_t bytes=99;
        auto bad=api;bad.recording_released=nullptr;
        Need(!pipeline::Create(h->device,bad,shape,code,SameDevice,64ull<<20,&bytes)&&bytes==0,"missing recording retirement proof rejected");
        Need(!pipeline::Create(h->device,api,shape,code,SameDevice,1,&bytes)&&bytes==0,"plane budget rejection before vendor allocation");
        context=pipeline::Create(h->device,api,shape,code,SameDevice,64ull<<20,&bytes);
        Need(context&&!pipeline::Poisoned(context)&&bytes>0&&allocations==4,"three scratch slots, one feature with private parameter blocks");
    }
    void RecordSpatial(ID3D12Resource* carrier,ID3D12Resource* output,ID3D12GraphicsCommandList* list) {
        if(!spatial_pipeline){
            // Test-only exact half-pixel reconstruction of the same N carrier
            // passed to Feature1. Production transport and Compose are unchanged.
            static constexpr char shader[]=R"(
                cbuffer Dims:register(b0){uint2 FullSize;uint2 WorkSize;};
                Texture2D<float4> Source:register(t0);
                RWTexture2D<float4> Dest:register(u0);
                [numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID){
                    if(any(id.xy>=FullSize))return;
                    uint2 numerator=(2*id.xy+1)*WorkSize+FullSize;
                    uint2 denominator=2*FullSize;
                    int2 origin=int2(numerator/denominator)-1;
                    float2 f=float2(numerator%denominator)/float2(denominator);
                    float3 sum=0;
                    [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){
                        int2 p=clamp(origin+int2(x,y),int2(0,0),int2(WorkSize)-1);
                        float w=(x?f.x:1-f.x)*(y?f.y:1-f.y);
                        sum+=Source.Load(int3(p,0)).rgb*w;
                    }
                    Dest[id.xy]=float4(sum,1);
                })";
            ID3DBlob *compiled=nullptr,*errors=nullptr,*signature=nullptr;
            const auto result=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,
                                         "main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&compiled,&errors);
            if(FAILED(result)){
                if(errors)std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize());
                throw std::runtime_error("quality spatial shader compilation");
            }
            if(errors)errors->Release();
            D3D12_DESCRIPTOR_RANGE ranges[2]{};
            ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
            ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
            D3D12_ROOT_PARAMETER parameters[3]{};
            for(unsigned n=0;n<2;++n){parameters[n].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                parameters[n].DescriptorTable={1,&ranges[n]};parameters[n].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;}
            parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[2].Constants={0,0,4};parameters[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
            D3D12_ROOT_SIGNATURE_DESC root_desc{3,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
            Check(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,
                                              &signature,&errors),"quality root signature serialization");
            if(errors)errors->Release();
            Check(h->device->CreateRootSignature(0,signature->GetBufferPointer(),
                                                 signature->GetBufferSize(),IID_PPV_ARGS(&spatial_root)),
                  "quality root signature");
            signature->Release();
            D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
            pipeline_desc.pRootSignature=spatial_root;
            pipeline_desc.CS={compiled->GetBufferPointer(),compiled->GetBufferSize()};
            Check(h->device->CreateComputePipelineState(&pipeline_desc,IID_PPV_ARGS(&spatial_pipeline)),
                  "quality spatial pipeline");
            compiled->Release();
            D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
            heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            heap_desc.NumDescriptors=2;heap_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            Check(h->device->CreateDescriptorHeap(&heap_desc,IID_PPV_ARGS(&spatial_heap)),
                  "quality spatial descriptors");
        }
        const auto stride=h->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto cpu=spatial_heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels=1;
        h->device->CreateShaderResourceView(carrier,&srv,cpu);
        cpu.ptr+=stride;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        h->device->CreateUnorderedAccessView(output,nullptr,&uav,cpu);
        ID3D12DescriptorHeap* heaps[]={spatial_heap};list->SetDescriptorHeaps(1,heaps);
        list->SetComputeRootSignature(spatial_root);list->SetPipelineState(spatial_pipeline);
        auto gpu=spatial_heap->GetGPUDescriptorHandleForHeapStart();
        list->SetComputeRootDescriptorTable(0,gpu);gpu.ptr+=stride;
        list->SetComputeRootDescriptorTable(1,gpu);
        const unsigned dims[]={width,height,nw,nh};
        list->SetComputeRoot32BitConstants(2,4,dims,0);
        list->Dispatch((width+7)/8,(height+7)/8,1);
    }
    ID3D12Resource* Image(float value,float alpha=.75f) {
        auto* result=h->Texture(*owner,width,height,10,0,0,false);auto d=result->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;h->device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
        auto* upload=h->Buffer(*owner,bytes,D3D12_HEAP_TYPE_UPLOAD);void* mapped=nullptr;D3D12_RANGE zero{};Check(upload->Map(0,&zero,&mapped),"image map");
        const auto pack=[](float f){const unsigned sign=std::signbit(f)?0x8000:0;float a=std::abs(f);if(a==0)return std::uint16_t(sign);
            int e=0;std::frexp(a,&e);--e;if(e< -14)return std::uint16_t(sign|unsigned(std::nearbyint(std::ldexp(a,24))));
            unsigned m=unsigned(std::nearbyint(std::ldexp(a,10-e)));if(m==2048){m=1024;++e;}
            return std::uint16_t(sign|unsigned(e+15)*1024|(m-1024));};
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned k=0;k<4;++k){auto half=pack(k==3?alpha:value);
            std::memcpy(static_cast<char*>(mapped)+y*fp.Footprint.RowPitch+x*8+k*2,&half,2);}
        upload->Unmap(0,nullptr);Transition(h->list,result,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=result;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        b.pResource=upload;b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=fp;
        h->list->CopyTextureRegion(&a,0,0,0,&b,nullptr);Transition(h->list,result,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);return result;
    }
    ID3D12Resource* Pattern(bool candidate) {
        auto* result=h->Texture(*owner,width,height,10,0,0,false);auto d=result->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
        h->device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
        auto* upload=h->Buffer(*owner,bytes,D3D12_HEAP_TYPE_UPLOAD);void* mapped=nullptr;D3D12_RANGE empty{};
        Check(upload->Map(0,&empty,&mapped),"quality pattern map");
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            // Same-frame B/Z: single-pixel strokes, a slanted edge, smooth
            // signed edit, neutral area and a sharp edit-support boundary.
            const bool line=(x>=350&&x<550&&y==70+(x-350)/4);
            const bool glyph=(x==480&&(y>=80&&y<=140))||
                (x>=480&&x<=496&&(y==80||y==110||y==140))||
                (x==496&&y>=80&&y<=140);
            const float edge=(x>410+y/5)?.09f:0.f;
            const float base=.40f+edge+(line||glyph?.22f:0.f);
            const float sx=(float(x)-320.f)/320.f,sy=(float(y)-192.f)/192.f;
            const float smooth=.035f*sx-.025f*sy;
            const float edit=x<width/2?0.f:smooth+(line||glyph?.03f:0.f);
            float value=base+(candidate?edit:0.f);
            if(dark_quality&&y>=300&&y<371&&x>=312&&x<561){
                // Separate low-luminance ROI. The support boundary remains
                // at x=320; the left strip has exactly zero source edit.
                constexpr unsigned char text_rows[4][7]={
                    {31,4,4,4,4,4,4},             // T
                    {31,16,16,30,16,16,31},      // E
                    {17,10,4,4,4,10,17},         // X
                    {31,4,4,4,4,4,4}};           // T
                const bool thin=x>=340&&x<541&&y==310+(x-340)/9;
                bool letters=false;
                if(x>=470&&x<494&&y>=336&&y<343){
                    const unsigned index=(x-470)/6,column=(x-470)%6;
                    letters=column<5&&((text_rows[index][y-336]>>(4-column))&1);
                }
                const float dark_base=.02f+(thin?.028f:0.f)+(letters?.035f:0.f);
                const float dark_edit=x<320?0.f:(thin?.006f:0.f)+(letters?-.007f:0.f)+.002f;
                value=dark_base+(candidate?dark_edit:0.f);
            }
            for(unsigned c=0;c<4;++c){
                const float v=c==3?.75f:value;
                int exponent=0;std::frexp(v,&exponent);--exponent;
                unsigned mantissa=unsigned(std::nearbyint(std::ldexp(v,10-exponent)));
                if(mantissa==2048){mantissa=1024;++exponent;}
                const auto half=std::uint16_t(unsigned(exponent+15)*1024+(mantissa-1024));
                std::memcpy(static_cast<char*>(mapped)+y*fp.Footprint.RowPitch+x*8+c*2,&half,2);}
        }
        upload->Unmap(0,nullptr);Transition(h->list,result,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=result;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
        h->list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        Transition(h->list,result,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);return result;
    }
    ID3D12Resource* DepthPlanes() {
        auto* result=h->Texture(*owner,nw,nh,41,0,0,false);auto d=result->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
        h->device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
        auto* upload=h->Buffer(*owner,bytes,D3D12_HEAP_TYPE_UPLOAD);void* mapped=nullptr;D3D12_RANGE empty{};
        Check(upload->Map(0,&empty,&mapped),"quality depth map");
        for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x){
            const float depth=x<nw/2?.25f:.75f;
            std::memcpy(static_cast<char*>(mapped)+y*fp.Footprint.RowPitch+x*4,&depth,4);
        }
        upload->Unmap(0,nullptr);
        Transition(h->list,result,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=result;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
        h->list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        Transition(h->list,result,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);return result;
    }
    void Submit(bool recorded,bool release_recording=true) {
        const auto recording=h->Rec();Check(h->list->Close(),"close");ID3D12CommandList* lists[]={h->list};
        h->queue->ExecuteCommandLists(1,lists);Check(h->queue->Signal(h->fence,++fence_value),"signal");
        if(recorded) {
            Need(pipeline::Submitted(context,recording,h->fence,{1,reinterpret_cast<std::uint64_t>(h->fence.p),fence_value},true),"exact submission accepted");
            Need(!pipeline::ReadyToRetire(context),"completed fence alone cannot retire replayable recording");
        }
        Check(h->fence->SetEventOnCompletion(fence_value,h->event),"wait event");
        Need(WaitForSingleObject(h->event,30000)==WAIT_OBJECT_0,"bounded completion wait in host only");
        Need(h->fence->GetCompletedValue()!=UINT64_MAX&&h->fence->GetCompletedValue()>=fence_value,"not removed fence");
        Check(h->allocator->Reset(),"retire allocator");Check(h->list->Reset(h->allocator,nullptr),"retire list");
        if(release_recording)retired.insert(recording.lifetime);++h->epoch;
        Need(pipeline::Collect(context),"collect non-failed flight");
    }
    pipeline::Input Frame(float candidate,bool alias=false) {
        pipeline::Input i{};i.pair.base=h->View(Image(.5f,.5f),10);i.pair.candidate=h->View(Image(candidate),10);
        i.target=alias?i.pair.candidate:h->View(Image(candidate),10);i.pair.base_rect=i.pair.candidate_rect=i.target_rect={0,0,width,height};
        i.pair.base_source=i.pair.candidate_source=i.guide_source={1,1,++sequence};
        i.motion_reference={1,1,sequence>1?sequence-1:0};i.pair.domain=nr_post_residual_dx12::Domain::SdrEncodedFp16;
        i.pair.same_encoding_verified=true;i.pair.work_width=nw;i.pair.work_height=nh;i.pair.carrier_range=1;
        i.depth=h->View(h->Texture(*owner,nw,nh,41,.5f),41);i.motion=h->View(h->Texture(*owner,nw,nh,16,0),16);
        i.recording=h->Rec();i.view=i.configuration=i.encoding_generation=1;
        i.target_contains_candidate=i.ordered_queue_verified=i.post_sr_phase_verified=i.normalized_guide_units_verified=true;
        i.native_result=1;i.reset=false;return i;
    }
    void PendingSecondList() {
        // A real second direct list on this device, not a fabricated new token
        // attached to the first list. No work from it may reach the shared SR
        // history before the first list has a successful submission receipt.
        ID3D12CommandAllocator* allocator=nullptr;ID3D12GraphicsCommandList* list=nullptr;
        Check(h->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"second allocator");
        Check(h->device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&list)),"second direct list");
        auto pending=Frame(.75f);pending.recording.list=reinterpret_cast<std::uint64_t>(list);
        pending.recording.lifetime+=10000;pending.recording.reset+=10000;
        const auto before=creates+evaluates;const auto retained=retained_resources;
        const auto answer=pipeline::Record(context,list,pending);
        Need(answer.status==pipeline::Status::Busy&&!answer.recorded&&!answer.composed&&creates+evaluates==before,
             "unsubmitted predecessor blocks second list before any transport or vendor work");
        Need(retained_resources==retained,"unsubmitted successor holds no extra guides");
        Check(list->Close(),"empty second list close");list->Release();allocator->Release();
    }
    void Invalid(pipeline::Input good) {
        const unsigned before=creates+evaluates;
        auto check=[&](pipeline::Input x){auto r=pipeline::Record(context,h->list,x);Need(r.status==pipeline::Status::InvalidContract&&!r.recorded&&!r.composed,"invalid input cannot record or consume private history");};
        auto x=good;x.native_result=0;check(x);x=good;x.target_contains_candidate=false;check(x);
        x=good;x.pair.candidate_source.sequence++;check(x);x=good;x.guide_source.sequence++;check(x);
        x=good;x.pair.domain=nr_post_residual_dx12::Domain::Unknown;check(x);x=good;x.pair.same_encoding_verified=false;check(x);
        x=good;x.post_sr_phase_verified=false;check(x);x=good;x.normalized_guide_units_verified=false;check(x);
        x=good;x.ordered_queue_verified=false;check(x);x=good;x.motion_scale_x=std::numeric_limits<float>::infinity();check(x);
        x=good;x.motion_scale_x=0;check(x);x=good;x.target=x.pair.base;check(x);
        x=good;x.pair.work_width++;check(x);x=good;x.recording.queue++;check(x);
        x=good;x.target_rect.x=1;check(x);x=good;x.encoding_generation=0;check(x);
        Need(creates+evaluates==before,"rejected frames leave opaque API untouched");
    }
    void Done() {
        Need(pipeline::ReadyToRetire(context)&&pipeline::Retire(context)&&!context,"full explicit retirement");
        Need(creates==1&&releases==1&&allocations==destroys,"one feature, all independent params destroyed");
        for(const auto& p:retained_resources)Need(p.second==0,"all completed guide and vendor references released exactly");
        // All commands completed and retired before resource destruction.
        Check(h->list->Close(),"empty final close");delete owner;owner=nullptr;delete h;h=nullptr;
        if(spatial_heap)spatial_heap->Release();
        if(spatial_pipeline)spatial_pipeline->Release();
        if(spatial_root)spatial_root->Release();
        if(bridge){FreeLibrary(bridge);bridge=nullptr;}
    }
};
void SaveReadback(const Harness::Readback& readback,const std::filesystem::path& file) {
    void* mapped=nullptr;Check(readback.buffer->Map(0,nullptr,&mapped),"quality readback map");
    std::ofstream out(file,std::ios::binary);Need(bool(out),"open quality plane");
    const auto row=std::size_t(readback.w)*PixelBytes(readback.format);
    for(unsigned y=0;y<readback.h;++y)
        out.write(static_cast<const char*>(mapped)+readback.footprint.Offset+
                  y*readback.footprint.Footprint.RowPitch,row);
    out.close();Need(bool(out),"write quality plane");
    D3D12_RANGE empty{};readback.buffer->Unmap(0,&empty);
}
void CaptureQuality(Rig& r,const std::filesystem::path& directory,float carrier_range) {
    Need(r.width==640&&r.height==384&&r.nw==320&&r.nh==192,
         "quality capture requires fixed 640x384 to 320x192 shape");
    std::filesystem::create_directories(directory);
    r.spatial_fixture=!r.real;
    std::ofstream manifest(directory/"frames.jsonl");Need(bool(manifest),"open quality identity log");
    for(unsigned frame=0;frame<16;++frame) {
        auto i=r.Frame(.5f);
        i.pair.base=r.h->View(r.Pattern(false),10);
        i.pair.candidate=r.h->View(r.Pattern(true),10);
        i.target=r.h->View(r.Pattern(true),10);
        i.depth=r.h->View(r.DepthPlanes(),41);
        i.pair.carrier_range=carrier_range;i.reset=frame==0;
        // A fixture copies a neutral reconstruction. The independent exact
        // spatial result is computed from these same captured N-grid planes.
        r.fixture_output=r.Image(.5f);
        auto result=pipeline::Record(r.context,r.h->list,i);
        Need(result.recorded&&result.composed&&result.status==pipeline::Status::Composed,
             "quality frame adopted by private SR");
        const auto planes=pipeline::InspectRecordedPlanes(r.context,result.slot,i.recording);
        Need(bool(planes),"exact recorded diagnostic planes");
        const std::array<const char*,7> names={"B","Z","carrier","tail","reconstructed","final","depth"};
        const std::array<ID3D12Resource*,7> resources={i.pair.base.resource,i.pair.candidate.resource,
            planes.carrier,planes.tail,planes.reconstructed,i.target.resource,i.depth.resource};
        const std::array<unsigned,7> formats={10,10,10,2,10,10,41};
        std::array<Harness::Readback,7> copies{};
        for(unsigned k=0;k<7;++k)copies[k]=r.h->CopyOut(*r.owner,resources[k],formats[k]);
        r.Submit(true);
        for(unsigned k=0;k<7;++k)
            SaveReadback(copies[k],directory/("frame-"+std::to_string(frame)+"-"+names[k]+
                         (formats[k]==2||formats[k]==41?".fp32":".fp16")));
        manifest<<"{\"frame\":"<<frame<<",\"source_sequence\":"<<i.pair.base_source.sequence
                <<",\"recording_lifetime\":"<<i.recording.lifetime
                <<",\"reset\":"<<(i.reset?"true":"false")
                <<",\"adopted\":true,\"mode\":\""<<(r.real?"feature1":"fixture")
                <<"\",\"output\":[640,384],\"work\":[320,192],\"carrier_range\":"<<carrier_range
                <<",\"depth\":\"two_planes_0.25_0.75_split_160\",\"scene\":\""
                <<(r.dark_quality?"dark_text_v1":"legacy_v1")<<"\"}\n";
        manifest.flush();Need(bool(manifest),"write quality identity log");
    }
}
void Run(const std::filesystem::path& shaders,bool hardware,const wchar_t* bridge,
         const std::wstring& negative,const std::filesystem::path& quality_dir,float carrier_range,bool dark_quality) {
    auto* r=new Rig(shaders,hardware,bridge,!quality_dir.empty());
    r->dark_quality=dark_quality;
    r->fixture_output=r->Image(.625f);r->Submit(false);
    auto warm=r->Frame(.75f);r->Invalid(warm);auto result=pipeline::Record(r->context,r->h->list,warm);
    Need(result.recorded&&!result.composed&&result.status==pipeline::Status::WaitingForSr,"creation is not successful reconstruction");
    if(negative==L"discard") {
        pipeline::Discarded(r->context,warm.recording);Need(pipeline::Poisoned(r->context)&&!pipeline::Retire(r->context),"discard quarantines uncertain vendor state");
        std::cout<<"PASS discarded recording remains retained; process exit is only cleanup\n";std::cout.flush();ExitProcess(0);
    }
    if(negative==L"submit") {
        Need(!pipeline::Submitted(r->context,warm.recording,r->h->fence,{2,reinterpret_cast<std::uint64_t>(r->h->fence.p),1},true),"wrong queue cannot publish");
        Need(pipeline::Poisoned(r->context)&&!pipeline::Retire(r->context),"failed submission is not safe retirement");
        std::cout<<"PASS wrong queue poisons without release; process exit is only cleanup\n";std::cout.flush();ExitProcess(0);
    }
    r->PendingSecondList();
    auto initial=r->h->CopyOut(*r->owner,warm.target.resource,10);
    r->hold_sr_completion=!r->real;
    r->Submit(true);r->h->Assert(initial,.75f,.75f, NAN,0);
    Need(r->evaluates==0,"no evaluate hidden in creation");
    if(!r->real) {
        auto waiting=r->Frame(.75f);auto pending=pipeline::Record(r->context,r->h->list,waiting);
        Need(pending.recorded&&!pending.composed&&r->evaluates==0,"waiting creation does not run SR or compose");
        Need(r->retained_resources[waiting.depth.resource]>0&&r->retained_resources[waiting.motion.resource]>0,
             "barrier-only waiting frame retains both guide resources");
        auto unchanged=r->h->CopyOut(*r->owner,waiting.target.resource,10);
        r->Submit(true);r->h->Assert(unchanged,.75f,.75f,NAN,0);
        r->hold_sr_completion=false;Need(pipeline::Collect(r->context),"creation completion becomes visible");
    }
    const float edits[]={0,1.f/1024,-1.f/2048,.25f,-.25f};
    for(unsigned kind=0;kind<5;++kind) {
        r->fixture_output=r->Image(kind==4?.375f:.625f);
        const unsigned count=r->real?4:1;
        for(unsigned frame=0;frame<count;++frame) {
            auto i=r->Frame(.5f+edits[kind],kind%2==1);
            i.pair.carrier_range=kind<=2?4.f:1.f;
            r->Invalid(i);auto v=pipeline::Record(r->context,r->h->list,i);
            Need(v.recorded&&v.composed&&v.status==pipeline::Status::Composed,"actual private evaluate precedes post composition");
            auto final=r->h->CopyOut(*r->owner,i.target.resource,10),base=r->h->CopyOut(*r->owner,i.pair.base.resource,10);
            const auto again=pipeline::Record(r->context,r->h->list,i);
            Need(!again.recorded&&!again.composed,"same source cannot be reconstructed twice");
            if(kind==4&&frame+1==count)r->PendingSecondList();
            r->Submit(true);
            r->h->Assert(base,.5f,.5f,NAN,0);
            // Exact neutrality and quantized tiny +/- changes must survive real
            // SR's possible carrier bias. Large signed SR signals have a stated
            // numeric tolerance, not an image-quality or temporal assertion.
            r->h->Assert(final,.5f+edits[kind],.5f,NAN,r->real&&kind>=3?.075f:0.f);
        }
    }
    if(!quality_dir.empty())CaptureQuality(*r,quality_dir,carrier_range);
    if(!r->real) {
        unsigned resets=r->reset_count;
        auto i=r->Frame(.75f);++i.encoding_generation;
        Need(pipeline::Record(r->context,r->h->list,i).composed,"encoding change accepts new frame");r->Submit(true);
        Need(r->reset_count==resets+1,"encoding identity resets private history");
        for(unsigned n=0;n<3;++n) {
            i=r->Frame(.75f);Need(pipeline::Record(r->context,r->h->list,i).composed,"independent free flight slot");r->Submit(true,false);
        }
        i=r->Frame(.75f);Need(pipeline::Record(r->context,r->h->list,i).status==pipeline::Status::Busy,"never overwrite three held scratch slots");
        r->Submit(false);for(std::uint64_t id=1;id<r->h->epoch;++id)r->retired.insert(id);
        Need(pipeline::Collect(r->context),"reclaim only after delayed recording retirement");
        i=r->Frame(.75f);Need(pipeline::Record(r->context,r->h->list,i).composed,"recover after normal capacity pressure");r->Submit(true);
    }
    r->Done();std::cout<<"PASS owned post-private-SR "<<(bridge?"actual-Feature1":hardware?"NVIDIA-fixture-API":"WARP-fixture-API")
        <<" checks="<<checks<<" creates="<<r->creates<<" evaluates="<<r->evaluates<<"; no game hook, Feature18 or moving-scene claim\n";delete r;
}
}
int wmain(int argc,wchar_t** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    try {
        if(argc<2)throw std::runtime_error("usage: post-private-sr <shaders> [--hardware|--real bridge|--negative submit|discard] [--quality-dir path] [--carrier-range value]");
        bool hardware=false,dark_quality=false;const wchar_t* real=nullptr;std::wstring negative;std::filesystem::path quality_dir;
        float carrier_range=4.f;
        for(int n=2;n<argc;++n) {
            if(std::wstring(argv[n])==L"--hardware")hardware=true;
            else if(std::wstring(argv[n])==L"--real"&&n+1<argc)real=argv[++n];
            else if(std::wstring(argv[n])==L"--negative"&&n+1<argc)negative=argv[++n];
            else if(std::wstring(argv[n])==L"--quality-dir"&&n+1<argc)quality_dir=argv[++n];
            else if(std::wstring(argv[n])==L"--quality-dark")dark_quality=true;
            else if(std::wstring(argv[n])==L"--carrier-range"&&n+1<argc){
                std::size_t parsed=0;carrier_range=std::stof(argv[++n],&parsed);
                if(parsed!=std::wstring(argv[n]).size()||!std::isfinite(carrier_range)||
                   carrier_range<=1e-6f||carrier_range>1e6f)
                    throw std::runtime_error("finite bounded carrier range");
            }
            else throw std::runtime_error("unsupported test argument");
        }
        post_sr_test::Run(argv[1],hardware,real,negative,quality_dir,carrier_range,dark_quality);return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"FAIL %s\n",e.what());std::fflush(nullptr);
        // Never unwind a pending/poisoned GPU fixture and release live objects.
        ExitProcess(1);
    }
}
