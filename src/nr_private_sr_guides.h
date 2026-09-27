#pragma once
// Product admission for the existing guide-normalization kernel. No NGX calls,
// resource allocation or lifetime ownership: the caller owns the recording lease.
#include "nr_guide_contract.h"
#if __has_include("private_sr/nr_edit_dx12.hpp")
#include "private_sr/nr_edit_dx12.hpp"
#else
#include "../experiments/nr-edit/src/nr_edit_dx12.hpp"
#endif
#include <cmath>

namespace nr_private_sr_guides {
enum class Reason { Ready, Disabled, Source, Recording, NativePhase, JitterUnknown, NonzeroJitter,
                    Flags, Extent, Units, Depth, Resource, Dispatch };
struct Input {
    bool enabled=false;
    nr_private_sr::Source source{}, snapshot_source{}, motion_reference{};
    nr_private_sr::Recording recording{}, snapshot_recording{};
    NrGuideSignature signature{};
    nr_edit_dx12::View depth{}, motion{};
    unsigned render_width=0,render_height=0,output_width=0,output_height=0;
    unsigned work_width=0,work_height=0;
    unsigned native_feature=0,native_flags=0;
    bool native_flags_known=false,read_boundary_snapshot=false;
    bool original_motion_scale_known=false,original_jitter_known=false,reset=true;
    float original_motion_scale_x=0,original_motion_scale_y=0;
    float original_jitter_x=0,original_jitter_y=0;
};
struct Constants {
    unsigned output[2]{},depth[2]{},motion[2]{},depth_origin[2]{},motion_origin[2]{};
    float motion_scale[2]{},projection_a=0,projection_b=0;
    unsigned linear_depth=0,inverted=0;
};
static_assert(sizeof(Constants)==64);
struct Plan { Reason reason=Reason::Source;Constants constants{};
    explicit operator bool()const{return reason==Reason::Ready;} };
inline bool Extent(unsigned w,unsigned h){return w&&h&&w<=16384&&h<=16384;}
inline bool Contains(unsigned long long w,unsigned h,unsigned x,unsigned y,unsigned rw,unsigned rh){
    return w<=16384&&h<=16384&&Extent(rw,rh)&&x<=w&&y<=h&&rw<=w-x&&rh<=h-y;
}
inline Plan Prepare(const Input& i) {
    Plan p;const auto reject=[&](Reason r){p.reason=r;return p;};
    if(!i.enabled)return reject(Reason::Disabled);
    if(!i.source||!(i.source==i.snapshot_source))return reject(Reason::Source);
    if(!i.reset&&(!i.motion_reference||i.motion_reference.stream!=i.source.stream||
       i.motion_reference.epoch!=i.source.epoch||i.motion_reference.sequence>=i.source.sequence||
       i.source.sequence-i.motion_reference.sequence!=1))return reject(Reason::Source);
    if(!i.recording||!(i.recording==i.snapshot_recording))return reject(Reason::Recording);
    if(!i.read_boundary_snapshot||i.native_feature!=1)
        return reject(Reason::NativePhase);
    if(!i.native_flags_known||(i.native_flags&~unsigned(2|8))!=0)return reject(Reason::Flags);
    // Non-jittered vectors do not prove unjittered spatial guide locations.
    // Until a producer proves de-jittered guide sampling, only known zero input
    // jitter is supported. Never turn missing jitter into zero or use NR's UI mode.
    if(!i.original_jitter_known||!std::isfinite(i.original_jitter_x)||!std::isfinite(i.original_jitter_y))
        return reject(Reason::JitterUnknown);
    if(i.original_jitter_x!=0||i.original_jitter_y!=0)return reject(Reason::NonzeroJitter);
    const auto& s=i.signature;
    if(!Extent(i.render_width,i.render_height)||!Extent(i.output_width,i.output_height)||
       !Extent(i.work_width,i.work_height)||i.work_width>i.output_width||i.work_height>i.output_height||
       s.depth_width!=i.render_width||s.depth_height!=i.render_height||
       !Contains(s.depth_physical_width,s.depth_physical_height,s.depth_x,s.depth_y,s.depth_width,s.depth_height)||
       !Contains(s.motion_physical_width,s.motion_physical_height,s.motion_x,s.motion_y,s.motion_source_width,s.motion_source_height))
        return reject(Reason::Extent);
    const bool low=(i.native_flags&2)!=0;
    if(s.motion_source_width!=(low?i.render_width:i.output_width)||
       s.motion_source_height!=(low?i.render_height:i.output_height))return reject(Reason::Extent);
    if(s.linear_depth||s.rr_depth_placeholder||s.depth_inverted!=int((i.native_flags&8)!=0))return reject(Reason::Depth);
    if(!i.original_motion_scale_known||!std::isfinite(i.original_motion_scale_x)||
       !std::isfinite(i.original_motion_scale_y)||i.original_motion_scale_x==0||i.original_motion_scale_y==0)
        return reject(Reason::Units);
    auto& c=p.constants;
    c.output[0]=i.work_width;c.output[1]=i.work_height;
    c.depth[0]=s.depth_width;c.depth[1]=s.depth_height;
    c.motion[0]=s.motion_source_width;c.motion[1]=s.motion_source_height;
    c.depth_origin[0]=s.depth_x;c.depth_origin[1]=s.depth_y;
    c.motion_origin[0]=s.motion_x;c.motion_origin[1]=s.motion_y;
    // NGX MV.Scale gives displacement in native render pixels, independent of
    // whether the vector field is stored at render or output resolution.
    c.motion_scale[0]=i.original_motion_scale_x*(float(i.work_width)/i.render_width);
    c.motion_scale[1]=i.original_motion_scale_y*(float(i.work_height)/i.render_height);
    if(!std::isfinite(c.motion_scale[0])||!std::isfinite(c.motion_scale[1])||
       c.motion_scale[0]==0||c.motion_scale[1]==0)return reject(Reason::Units);
    c.inverted=s.depth_inverted;p.reason=Reason::Ready;return p;
}
struct Result {
    Reason reason=Reason::Resource;
    nr_edit_dx12::Answer dispatch{};
    nr_private_sr::Source source{},motion_reference{};
    nr_private_sr::Recording recording{};
    bool normalized_guide_units_verified=false,post_sr_phase_verified=false;
};
struct FinishProof {
    nr_private_sr::Source native_source{},base_source{},candidate_source{};
    nr_private_sr::Recording recording{};
    unsigned native_feature=0,native_result=0;
    bool native_output_phase_verified=false;
};
// Called after native SR and B/Z production. This never records work and cannot
// repair a refused capture; pre-native guide leases remain owned on failure.
inline Result Finish(const Input& capture,Result recorded,const FinishProof& proof) {
    recorded.post_sr_phase_verified=false;
    if(recorded.reason!=Reason::Ready||!recorded.dispatch.recorded||
       !recorded.normalized_guide_units_verified)return recorded;
    if(proof.native_feature!=1||proof.native_result!=1||!proof.native_output_phase_verified||
       !(proof.native_source==recorded.source)||!(proof.base_source==recorded.source)||
       !(proof.candidate_source==recorded.source)||!(recorded.source==capture.source)||
       !(proof.recording==capture.recording)||!(recorded.recording==capture.recording)){
        recorded.reason=Reason::NativePhase;return recorded;
    }
    recorded.post_sr_phase_verified=true;return recorded;
}
// Outputs must be distinct zero-origin, exact N-sized R32_FLOAT/R32G32_FLOAT
// textures. Kernel Record pins all four resources, restores states and retains
// its immutable descriptors until caller-supplied submit + recording-release.
Result Record(nr_edit_dx12::Kernels*,ID3D12GraphicsCommandList*,const Input&,
              const nr_edit_dx12::View& depth_out,const nr_edit_dx12::View& motion_out);
}
