#include "../src/nr_private_sr_guides.h"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace guide_test {
using namespace nr_private_sr_guides;
unsigned checks=0;
void Check(bool v){++checks;if(!v)throw std::runtime_error("guide contract check "+std::to_string(checks));}
Input Valid(){
    Input i;i.enabled=true;i.source=i.snapshot_source={7,2,9};i.motion_reference={7,2,8};
    i.recording=i.snapshot_recording={1,1,3,4,5};i.native_feature=1;i.native_flags=2;
    i.native_flags_known=i.read_boundary_snapshot=i.original_motion_scale_known=i.original_jitter_known=true;
    i.reset=false;i.original_motion_scale_x=2;i.original_motion_scale_y=-3;
    i.render_width=8;i.render_height=6;i.output_width=16;i.output_height=12;i.work_width=4;i.work_height=3;
    auto& s=i.signature;s.depth_physical_width=s.motion_physical_width=12;
    s.depth_physical_height=s.motion_physical_height=10;
    s.depth_width=s.motion_source_width=8;s.depth_height=s.motion_source_height=6;
    s.depth_x=2;s.depth_y=1;s.motion_x=3;s.motion_y=2;return i;
}
void Cpu(){
    auto i=Valid();auto p=Prepare(i);Check(bool(p));Check(p.constants.motion_scale[0]==1&&p.constants.motion_scale[1]==-1.5f);
    Check(p.constants.depth_origin[0]==2&&p.constants.motion_origin[1]==2);
    i.signature.motion_scale_x=999;Check(Prepare(i).constants.motion_scale[0]==1);
    i=Valid();i.enabled=false;i.source={};Check(Prepare(i).reason==Reason::Disabled);
    i=Valid();i.snapshot_source.sequence++;Check(Prepare(i).reason==Reason::Source);
    i=Valid();i.snapshot_recording.reset++;Check(Prepare(i).reason==Reason::Recording);
    i=Valid();i.motion_reference.sequence-=2;Check(Prepare(i).reason==Reason::Source);
    i=Valid();i.motion_reference.epoch++;Check(Prepare(i).reason==Reason::Source);
    i=Valid();i.reset=true;i.motion_reference={};Check(bool(Prepare(i)));
    i=Valid();i.read_boundary_snapshot=false;Check(Prepare(i).reason==Reason::NativePhase);
    i=Valid();i.native_feature=13;Check(bool(Prepare(i)));
    i=Valid();i.native_flags_known=false;Check(Prepare(i).reason==Reason::Flags);
    for(unsigned flag:{4u,128u}){i=Valid();i.native_flags|=flag;Check(Prepare(i).reason==Reason::Flags);}
    i=Valid();i.original_jitter_known=false;Check(Prepare(i).reason==Reason::JitterUnknown);
    i=Valid();i.original_jitter_x=.125f;Check(Prepare(i).reason==Reason::NonzeroJitter);
    i.raster_convention=RasterConvention::NgxViewportOffset;Check(bool(Prepare(i)));
    Check(Prepare(i).constants.raster_offset[0]==.125f);
    i.native_flags=75;i.signature.depth_inverted=1;Check(bool(Prepare(i)));
    i.original_jitter_x=-.5f;i.original_jitter_y=.5f;Check(bool(Prepare(i)));
    i.original_jitter_x=.51f;Check(Prepare(i).reason==Reason::NonzeroJitter);
    i=Valid();i.native_feature=13;i.signature.linear_depth=true;
    i.signature.depth_projection_a=1;i.signature.depth_projection_b=-.1f;Check(bool(Prepare(i)));
    Check(Prepare(i).constants.linear_depth==1&&Prepare(i).constants.projection_b==-.1f);
    i=Valid();i.original_jitter_y=std::numeric_limits<float>::quiet_NaN();Check(Prepare(i).reason==Reason::JitterUnknown);
    i=Valid();i.original_motion_scale_known=false;Check(Prepare(i).reason==Reason::Units);
    i=Valid();i.original_motion_scale_x=0;Check(Prepare(i).reason==Reason::Units);
    i=Valid();i.original_motion_scale_y=std::numeric_limits<float>::infinity();Check(Prepare(i).reason==Reason::Units);
    i=Valid();i.signature.depth_x=8;Check(Prepare(i).reason==Reason::Extent);
    i=Valid();i.signature.motion_y=8;Check(Prepare(i).reason==Reason::Extent);
    i=Valid();i.signature.motion_source_width=16;Check(Prepare(i).reason==Reason::Extent);
    i=Valid();i.output_width=257;i.output_height=193;i.work_width=64;i.work_height=48;
    Check(bool(Prepare(i)));i.work_width=63;Check(Prepare(i).reason==Reason::Extent);
    i.work_width=64;i.work_height=47;Check(Prepare(i).reason==Reason::Extent);
    i=Valid();i.work_width=0;Check(Prepare(i).reason==Reason::Extent);
    i=Valid();i.work_width=17;Check(Prepare(i).reason==Reason::Extent);
    i=Valid();i.signature.linear_depth=true;Check(Prepare(i).reason==Reason::Depth);
    i=Valid();i.signature.rr_depth_placeholder=true;Check(Prepare(i).reason==Reason::Depth);
    i=Valid();i.signature.depth_inverted=1;Check(Prepare(i).reason==Reason::Depth);
    i.native_flags|=8;Check(Prepare(i).constants.inverted==1);
    i=Valid();i.native_flags=0;i.signature.motion_physical_width=20;i.signature.motion_physical_height=16;
    i.signature.motion_source_width=16;i.signature.motion_source_height=12;Check(bool(Prepare(i)));
    Check(Prepare(i).constants.motion_scale[0]==1); // field resolution is not displacement units
    i=Valid();Result r;r.reason=Reason::Ready;r.dispatch.recorded=true;r.source=i.source;r.recording=i.recording;r.normalized_guide_units_verified=true;
    FinishProof f{i.source,i.source,i.source,i.recording,1,1,true};Check(Finish(i,r,f).post_sr_phase_verified);
    f.native_result=0;auto rejected=Finish(i,r,f);Check(!rejected.post_sr_phase_verified&&rejected.dispatch.recorded);
    f.native_result=1;f.candidate_source.sequence++;Check(Finish(i,r,f).reason==Reason::NativePhase);
    f.candidate_source=i.source;f.recording.reset++;Check(Finish(i,r,f).reason==Reason::NativePhase);
    f.recording=i.recording;r.dispatch.recorded=false;Check(!Finish(i,r,f).post_sr_phase_verified);
}
}
#ifndef NR_PRIVATE_SR_GUIDES_TEST_LIBRARY
int main(){try{guide_test::Cpu();std::cout<<"guide CPU checks="<<guide_test::checks<<" passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
