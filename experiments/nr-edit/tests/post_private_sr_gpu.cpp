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
    std::map<void*,unsigned> retained_resources;
    std::array<std::vector<char>,2> shader_bytes;

    Rig(const std::filesystem::path& shaders,bool hardware,const wchar_t* real_bridge)
        :width(real_bridge?640:64),height(real_bridge?384:48),nw(width/2),nh(height/2),real(real_bridge!=nullptr) {
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
        if(bridge){FreeLibrary(bridge);bridge=nullptr;}
    }
};
void Run(const std::filesystem::path& shaders,bool hardware,const wchar_t* bridge,const std::wstring& negative) {
    auto* r=new Rig(shaders,hardware,bridge);
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
            r->Submit(true);
            r->h->Assert(base,.5f,.5f,NAN,0);
            // Exact neutrality and quantized tiny +/- changes must survive real
            // SR's possible carrier bias. Large signed SR signals have a stated
            // numeric tolerance, not an image-quality or temporal assertion.
            r->h->Assert(final,.5f+edits[kind],.5f,NAN,r->real&&kind>=3?.075f:0.f);
        }
    }
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
        if(argc<2)throw std::runtime_error("usage: post-private-sr <shaders> [--hardware|--real bridge|--negative submit|discard]");
        bool hardware=false;const wchar_t* real=nullptr;std::wstring negative;
        for(int n=2;n<argc;++n) {
            if(std::wstring(argv[n])==L"--hardware")hardware=true;
            else if(std::wstring(argv[n])==L"--real"&&n+1<argc)real=argv[++n];
            else if(std::wstring(argv[n])==L"--negative"&&n+1<argc)negative=argv[++n];
            else throw std::runtime_error("unsupported test argument");
        }
        post_sr_test::Run(argv[1],hardware,real,negative);return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"FAIL %s\n",e.what());std::fflush(nullptr);
        // Never unwind a pending/poisoned GPU fixture and release live objects.
        ExitProcess(1);
    }
}
