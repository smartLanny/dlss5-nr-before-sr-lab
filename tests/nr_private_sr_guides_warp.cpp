#define NR_EDIT_SHADER_VALIDATION_LIBRARY
#include "../experiments/nr-edit/tests/windows_shader_validation.cpp"
#define NR_PRIVATE_SR_GUIDES_TEST_LIBRARY
#include "nr_private_sr_guides_cpu.cpp"
namespace {
void GuideCase(Harness& h,bool high,bool inverted,bool invalid){
    using namespace nr_private_sr_guides;ResourceOwner owner;auto i=guide_test::Valid();
    i.recording=i.snapshot_recording=h.Rec();i.native_flags=(high?0:2)|(inverted?8:0);
    auto& s=i.signature;s.depth_inverted=inverted;
    s.depth_physical_width=12;s.depth_physical_height=10;
    s.motion_physical_width=high?20:12;s.motion_physical_height=high?16:10;
    s.motion_source_width=high?16:8;s.motion_source_height=high?12:6;
    s.depth_x=1;
    // Alternating atlas columns distinguish correct active origins and field
    // resolution from blindly sampling allocation dimensions or origin zero.
    const float sample=invalid?std::numeric_limits<float>::quiet_NaN():2.f;
    auto* d=h.Texture(owner,12,10,41,invalid?std::numeric_limits<float>::quiet_NaN():.25f,.75f,true,!invalid);
    auto* m=h.Texture(owner,unsigned(s.motion_physical_width),s.motion_physical_height,16,sample,.75f,true,!invalid);
    auto* od=h.Texture(owner,4,3,41,0,0,false);auto* om=h.Texture(owner,4,3,16,0,0,false);
    i.depth=h.View(d,41);i.motion=h.View(m,16);
    auto out=Record(h.kernels,h.list,i,h.View(od,41),h.View(om,16));
    guide_test::Check(out.dispatch.recorded&&out.normalized_guide_units_verified&&!out.post_sr_phase_verified);
    auto rd=h.CopyOut(owner,od,41);auto rm=h.CopyOut(owner,om,16);h.Submit();
    h.Assert(rd,invalid?(inverted?0.f:1.f):1.f);
    void* data=nullptr;Check(rm.buffer->Map(0,nullptr,&data),"guide motion map");
    for(unsigned y=0;y<3;y++)for(unsigned x=0;x<4;x++){
        float mv[2];std::memcpy(mv,static_cast<char*>(data)+rm.footprint.Offset+y*rm.footprint.Footprint.RowPitch+x*8,8);
        guide_test::Check(mv[0]==(invalid?8.f:high?2.f:1.f)&&mv[1]==(invalid?6.f:high?-3.f:-1.5f));
    }
    D3D12_RANGE no_write{0,0};rm.buffer->Unmap(0,&no_write);h.Reset();
}
// Alternating atlas values provide an independent discrete sampling oracle:
// N=R makes +.5 move to the next raster sample and -.5 retain the current one.
// Different atlas origins ensure depth and MV use the same *local* sample.
void DynamicPhase(Harness& h,float jitter){
    using namespace nr_private_sr_guides;ResourceOwner owner;auto i=guide_test::Valid();
    i.recording=i.snapshot_recording=h.Rec();i.native_flags=75;i.signature.depth_inverted=1;
    i.raster_convention=RasterConvention::NgxViewportOffset;
    i.original_jitter_x=jitter;i.original_jitter_y=-jitter;
    i.work_width=8;i.work_height=6;
    auto* d=h.Texture(owner,12,10,41,.25f,.75f,true,true);
    auto* m=h.Texture(owner,12,10,16,2.f,.75f,true,true);
    auto* od=h.Texture(owner,8,6,41,0,0,false);auto* om=h.Texture(owner,8,6,16,0,0,false);
    i.depth=h.View(d,41);i.motion=h.View(m,16);
    auto result=Record(h.kernels,h.list,i,h.View(od,41),h.View(om,16));guide_test::Check(result.dispatch.recorded);
    auto rd=h.CopyOut(owner,od,41),rm=h.CopyOut(owner,om,16);h.Submit();
    void *dp=nullptr,*mp=nullptr;Check(rd.buffer->Map(0,nullptr,&dp),"phase depth Map");Check(rm.buffer->Map(0,nullptr,&mp),"phase MV Map");
    for(unsigned y=0;y<6;y++)for(unsigned x=0;x<8;x++){
        const unsigned local=std::min(x+(jitter==.5f?1u:0u),7u);
        const float depth=(local+2)%2?.25f:1.f,motion=(local+3)%2?2.f:1.f;
        float z=0,mv[2]{};
        std::memcpy(&z,static_cast<char*>(dp)+rd.footprint.Offset+y*rd.footprint.Footprint.RowPitch+x*4,4);
        std::memcpy(mv,static_cast<char*>(mp)+rm.footprint.Offset+y*rm.footprint.Footprint.RowPitch+x*8,8);
        guide_test::Check(z==depth&&mv[0]==motion*2&&mv[1]==motion*-3);
    }
    D3D12_RANGE no_write{0,0};rd.buffer->Unmap(0,&no_write);rm.buffer->Unmap(0,&no_write);h.Reset();
}
void Refusals(Harness& h){
    using namespace nr_private_sr_guides;ResourceOwner owner;auto i=guide_test::Valid();i.recording=i.snapshot_recording=h.Rec();
    auto* d=h.Texture(owner,12,10,41,.5f);auto* m=h.Texture(owner,12,10,16,1);
    auto* od=h.Texture(owner,4,3,41,0,0,false);auto* om=h.Texture(owner,4,3,16,0,0,false);
    i.depth=h.View(d,41);i.motion=h.View(m,16);
    i.original_jitter_x=.5f;guide_test::Check(Record(h.kernels,h.list,i,h.View(od,41),h.View(om,16)).reason==Reason::NonzeroJitter);
    i.original_jitter_x=0;guide_test::Check(!Record(h.kernels,h.list,i,h.View(od,41),h.View(od,41)).dispatch.recorded);
    auto* wrong=h.Texture(owner,5,3,41,0,0,false);
    guide_test::Check(Record(h.kernels,h.list,i,h.View(wrong,41),h.View(om,16)).reason==Reason::Resource);
    i.enabled=false;guide_test::Check(Record(nullptr,nullptr,i,{},{}).reason==Reason::Disabled);
    i.enabled=true;guide_test::Check(Record(h.kernels,h.list,i,h.View(od,41),h.View(om,16)).dispatch.recorded);
    // Uploads are real commands even when earlier normalization attempts refuse.
    h.Submit();h.Reset();
}
}
int wmain(int argc,wchar_t**argv){try{
    if(argc!=2)throw std::runtime_error("usage: guides-warp <compiled-shader-directory>");
    guide_test::Cpu();Harness h{std::filesystem::path(argv[1]),false};
    GuideCase(h,false,false,false);GuideCase(h,true,true,false);
    GuideCase(h,false,false,true);GuideCase(h,true,true,true);Refusals(h);
    for(float j:{.5f,-.5f,.25f,-.25f})DynamicPhase(h,j);
    std::cout<<"private guides CPU + WARP checks="<<guide_test::checks<<" passed; no model/game acceptance\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
