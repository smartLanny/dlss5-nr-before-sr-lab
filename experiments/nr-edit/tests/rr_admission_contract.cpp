#include "nr_rr_admission.hpp"
#include <cstdlib>
#include <cstdio>
#include <limits>

using namespace nr_rr_dx12;
static int checks=0;
static void Expect(Admission actual,Admission expected,const char* name){
    ++checks;
    if(actual!=expected){std::fprintf(stderr,"FAIL %s actual=%u expected=%u\n",name,unsigned(actual),unsigned(expected));std::exit(1);}
}
static Input Good(const nr_private_sr::Shape& s){
    Input i{};
    i.recording={s.device,s.queue,31,41,51};i.source={61,71,81};
    i.view=91;i.configuration=101;i.color_domain=111;
    i.scene_linear_verified=i.normalized_guides_verified=i.ordered_queue_verified=true;
    i.constants.render_size[0]=s.render_width;i.constants.render_size[1]=s.render_height;
    i.constants.output_size[0]=s.output_width;i.constants.output_size[1]=s.output_height;
    i.constants.frame_pair_valid=1;i.constants.camera_mapping_verified=1;
    return i;
}
int main(){
    const nr_private_sr::Shape s{321,193,641,385,1,0,11,21,1};
    auto base=Good(s);Expect(AdmitScalars(base,s),Admission::Accepted,"valid odd extent");
    auto t=base;t.recording.reset=0;Expect(AdmitScalars(t,s),Admission::Recording,"missing reset generation");
    t=base;t.recording.queue++;Expect(AdmitScalars(t,s),Admission::Recording,"wrong queue");
    t=base;t.source.epoch=0;Expect(AdmitScalars(t,s),Admission::Source,"missing source epoch");
    t=base;t.scene_linear_verified=false;Expect(AdmitScalars(t,s),Admission::Domain,"unproved color domain");
    t=base;t.normalized_guides_verified=false;Expect(AdmitScalars(t,s),Admission::Guides,"unproved guides");
    t=base;t.constants.output_size[0]++;Expect(AdmitScalars(t,s),Admission::Extent,"wrong output extent");
    t=base;t.constants.frame_pair_valid=0;Expect(AdmitScalars(t,s),Admission::FramePair,"unpaired frame");
    t=base;t.constants.force_identity=1;Expect(AdmitScalars(t,s),Admission::Identity,"forced identity");
    t=base;t.constants.input_pre_exposure=0;Expect(AdmitScalars(t,s),Admission::Exposure,"zero exposure");
    t=base;t.constants.new_sample_rate=1.1f;Expect(AdmitScalars(t,s),Admission::SampleRate,"invalid sample weight");
    t=base;t.constants.depth_relative_tolerance=-1;Expect(AdmitScalars(t,s),Admission::DepthTolerance,"invalid depth tolerance");
    t=base;t.constants.static_motion_tolerance_pixels=std::numeric_limits<float>::quiet_NaN();Expect(AdmitScalars(t,s),Admission::MotionTolerance,"nonfinite motion tolerance");
    t=base;t.constants.explicit_previous_depth_verified=1;Expect(AdmitScalars(t,s),Admission::PreviousDepth,"claimed previous depth without resource");
    t=base;t.constants.camera_mapping_verified=0;Expect(AdmitScalars(t,s),Admission::Reprojection,"no temporal map");
    t.allow_spatial_only=true;Expect(AdmitScalars(t,s),Admission::Accepted,"spatial fallback explicitly allowed");
    t=base;t.constants.current_clip_to_previous_clip[3]=std::numeric_limits<float>::infinity();Expect(AdmitScalars(t,s),Admission::Reprojection,"nonfinite reprojection");
    std::printf("PASS rr_admission_contract checks=%d (CPU scalar admission only; no Feature13)\n",checks);
}
