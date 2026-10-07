// Bounded real-Feature1 attribution host. All jitter overrides are test-only
// and explicitly recorded; the product post-SR phase remains unchanged.
#define wmain legacy_private_sr_wmain
#include "post_private_sr_gpu.cpp"
#undef wmain
#include <functional>

namespace attribution {
using namespace post_sr_test;
Rig::Evaluate original_evaluate=nullptr;
float sample_jitter_x=0,sample_jitter_y=0;
unsigned EvaluateWithMatchedJitter(ID3D12GraphicsCommandList* list,const void* handle,
                                   const NVSDK_NGX_Parameter* params,void* callback) {
    auto* writable=const_cast<NVSDK_NGX_Parameter*>(params);
    writable->Set("Jitter.Offset.X",sample_jitter_x);
    writable->Set("Jitter.Offset.Y",sample_jitter_y);
    return original_evaluate(list,handle,params,callback);
}
std::uint16_t Pack(float f) {
    const unsigned sign=std::signbit(f)?0x8000:0;const float a=std::abs(f);
    if(!a)return std::uint16_t(sign);
    int e=0;std::frexp(a,&e);--e;
    if(e< -14)return std::uint16_t(sign|unsigned(std::nearbyint(std::ldexp(a,24))));
    unsigned m=unsigned(std::nearbyint(std::ldexp(a,10-e)));
    if(m==2048){m=1024;++e;}
    return std::uint16_t(sign|unsigned(e+15)*1024|(m-1024));
}
ID3D12Resource* Pattern(Rig& r,bool candidate,const std::wstring& scenario,unsigned frame) {
    if(scenario==L"frozen")return r.Pattern(candidate);
    auto* image=r.h->Texture(*r.owner,r.width,r.height,10,0,0,false);
    auto d=image->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
    r.h->device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
    auto* upload=r.h->Buffer(*r.owner,bytes,D3D12_HEAP_TYPE_UPLOAD);void* mapped=nullptr;
    Check(upload->Map(0,nullptr,&mapped),"attribution image map");
    const float positive=(96.f+4.f*frame)/65536.f;
    const float negative=-(40.f+4.f*frame)/65536.f;
    for(unsigned y=0;y<r.height;++y)for(unsigned x=0;x<r.width;++x){
        const float px=float(x)+sample_jitter_x*float(r.width)/r.nw;
        const float py=float(y)+sample_jitter_y*float(r.height)/r.nh;
        float base=.015625f;float edit[3]{};
        if(scenario==L"threshold") {
            const bool patch=x>=r.width/3&&x<2*r.width/3&&y>=r.height/4&&y<r.height/2;
            const bool thin=x>=r.width/3&&x<2*r.width/3&&y==3*r.height/4+(x-r.width/3)/32;
            if(patch||thin){edit[0]=positive;edit[1]=negative;edit[2]=positive*.5f;}
        }else {
            // Continuous world signal sampled at N-pixel jitter. The scene and
            // depth are static, so unjittered scene motion is exactly zero.
            const float sx=px*640.f/r.width,sy=py*384.f/r.height;
            const float line=std::max(0.f,1.f-std::abs(sy-310.f-(sx-340.f)/9.f));
            const float curve=std::max(0.f,1.f-std::abs(sy-348.f-(sx-400.f)*(sx-400.f)/115.f));
            const bool inside=sx>=330&&sx<561&&sy>=300&&sy<371;
            base=inside?.02f+.028f*line+.026f*curve:.25f;
            if(inside)for(unsigned c=0;c<3;++c)edit[c]=.002f+.006f*line+.004f*curve;
        }
        for(unsigned c=0;c<4;++c){auto q=Pack(c==3?.75f:base+(candidate?edit[c]:0.f));
            std::memcpy(static_cast<char*>(mapped)+y*fp.Footprint.RowPitch+x*8+c*2,&q,2);}
    }
    upload->Unmap(0,nullptr);
    Transition(r.h->list,image,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=image;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
    r.h->list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Transition(r.h->list,image,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);return image;
}
void ReleaseFrame(Rig& r,std::size_t start) {
    // Submit already proved exact completion and collected the original flight.
    while(r.owner->values.size()>start){r.owner->values.back()->Release();r.owner->values.pop_back();}
}
void Run(const std::filesystem::path& shaders,const wchar_t* bridge,const std::filesystem::path& dir,
         unsigned ow,unsigned oh,unsigned nw,unsigned nh,const std::wstring& scenario,bool per_channel_support) {
    Need(scenario==L"frozen"||scenario==L"threshold"||scenario==L"matched-jitter","known scenario");
    auto* r=new Rig(shaders,true,bridge,true,ow,oh,nw,nh);r->dark_quality=true;
    original_evaluate=r->evaluate;if(scenario==L"matched-jitter")r->evaluate=EvaluateWithMatchedJitter;
    std::filesystem::create_directories(dir);std::ofstream log(dir/"frames.jsonl");
    Need(bool(log),"open attribution log");
    constexpr unsigned warmup=16,captured=16;
    for(unsigned step=0;step<=warmup+captured;++step){
        const auto start=r->owner->values.size();
        const unsigned frame=step>warmup?step-warmup-1:0;
        if(scenario==L"matched-jitter"){
            constexpr float jx[]={-.375f,.125f,.375f,-.125f},jy[]={-.125f,.375f,-.375f,.125f};
            sample_jitter_x=jx[(step?step-1:0)%4];sample_jitter_y=jy[(step?step-1:0)%4];
        }
        pipeline::Input i{};i.pair.base=r->h->View(Pattern(*r,false,scenario,frame),10);
        i.pair.candidate=r->h->View(Pattern(*r,true,scenario,frame),10);i.target=r->h->View(Pattern(*r,true,scenario,frame),10);
        i.pair.base_rect=i.pair.candidate_rect=i.target_rect={0,0,ow,oh};
        i.pair.base_source=i.pair.candidate_source=i.guide_source={1,1,++r->sequence};
        i.motion_reference={1,1,r->sequence>1?r->sequence-1:0};
        i.pair.domain=nr_post_residual_dx12::Domain::SdrEncodedFp16;i.pair.same_encoding_verified=true;
        i.pair.work_width=nw;i.pair.work_height=nh;i.pair.carrier_range=1;
        i.per_channel_support=per_channel_support;
        i.depth=r->h->View(r->h->Texture(*r->owner,nw,nh,41,.5f),41);
        i.motion=r->h->View(r->h->Texture(*r->owner,nw,nh,16,0),16);
        i.recording=r->h->Rec();i.view=i.configuration=i.encoding_generation=1;i.native_result=1;
        i.target_contains_candidate=i.ordered_queue_verified=i.post_sr_phase_verified=i.normalized_guide_units_verified=true;
        i.reset=step==1;
        auto result=pipeline::Record(r->context,r->h->list,i);
        Need(result.recorded&&(step==0?!result.composed:result.composed),"bounded actual Feature1 record");
        if(step<=warmup){r->Submit(true);ReleaseFrame(*r,start);continue;}
        Need(result.bind.bound&&result.bind.reset_causes==0,"captured frames retain warmed continuous history");
        const auto p=pipeline::InspectRecordedPlanes(r->context,result.slot,i.recording);Need(bool(p),"original flight planes");
        auto* spatial=r->h->Texture(*r->owner,ow,oh,10,0,0,false);
        Transition(r->h->list,p.carrier,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(r->h->list,spatial,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        r->RecordSpatial(p.carrier,spatial,r->h->list);
        D3D12_RESOURCE_BARRIER u{};u.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;u.UAV.pResource=spatial;r->h->list->ResourceBarrier(1,&u);
        Transition(r->h->list,spatial,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
        Transition(r->h->list,p.carrier,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        auto* sf=Pattern(*r,true,scenario,frame);
        Need(pipeline::RecordSpatialReference(r->context,r->h->list,result.slot,i.recording,r->h->View(spatial,10),r->h->View(sf,10),i.target_rect),"matched FP16 spatial Compose");
        const std::array<const char*,10> names={"B","Z","carrier","tail","reconstructed","spatial_reconstructed","final","spatial_final","depth","motion"};
        const std::array<ID3D12Resource*,10> resources={i.pair.base.resource,i.pair.candidate.resource,p.carrier,p.tail,p.reconstructed,spatial,i.target.resource,sf,i.depth.resource,i.motion.resource};
        const std::array<unsigned,10> formats={10,10,10,2,10,10,10,10,41,16};std::array<Harness::Readback,10> copies{};
        for(unsigned k=0;k<10;++k)copies[k]=r->h->CopyOut(*r->owner,resources[k],formats[k]);r->Submit(true);
        for(unsigned k=0;k<10;++k)SaveReadback(copies[k],dir/("frame-"+std::to_string(frame)+"-"+names[k]+(formats[k]==10?".fp16":".fp32")));
        log<<"{\"frame\":"<<frame<<",\"source_sequence\":"<<i.pair.base_source.sequence<<",\"predecessor_sequence\":"<<result.bind.predecessor.sequence
           <<",\"reset_causes\":"<<result.bind.reset_causes<<",\"history_causes\":"<<result.bind.history_causes<<",\"feature1_bind_reset\":false,\"mode\":\"feature1\",\"adopted\":true"
           <<",\"output\":["<<ow<<","<<oh<<"],\"work\":["<<nw<<","<<nh<<"],\"carrier_range\":1,\"warmup_evaluations\":16,\"scenario\":\""<<std::string(scenario.begin(),scenario.end())
           <<"\",\"effective_ngx_jitter\":["<<sample_jitter_x<<","<<sample_jitter_y<<"],\"sample_offset_output\":["<<sample_jitter_x*ow/nw<<","<<sample_jitter_y*oh/nh
           <<"],\"depth\":\"normalized_constant_0.5\",\"motion\":\"normalized_zero_work_pixels\",\"fence_value\":"<<r->fence_value<<",\"completion_value\":"<<r->h->fence->GetCompletedValue()
           <<",\"recording_released\":true,\"per_channel_support\":"<<(per_channel_support?"true":"false")
           <<",\"encoding_policy\":\"post_legacy_range4_neutral\",\"zero_preserve\":true}\n";log.flush();ReleaseFrame(*r,start);
    }
    r->Done();std::cout<<"PASS warmed real Feature1 attribution captured=16 warmup=16 scenario="<<std::string(scenario.begin(),scenario.end())<<" checks="<<checks<<'\n';delete r;
}
}
int wmain(int argc,wchar_t** argv){try{if(argc!=10&&argc!=11)throw std::runtime_error("usage: host shaders bridge capture OW OH NW NH scenario --bounded16 [--legacy-rgb-support]");
    if(std::wstring(argv[9])!=L"--bounded16")throw std::runtime_error("bounded16 required");
    if(argc==11&&std::wstring(argv[10])!=L"--legacy-rgb-support")throw std::runtime_error("unknown support option");
    attribution::Run(argv[1],argv[2],argv[3],std::stoul(argv[4]),std::stoul(argv[5]),std::stoul(argv[6]),std::stoul(argv[7]),argv[8],argc==10);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());std::fflush(nullptr);ExitProcess(1);}}
