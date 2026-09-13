#pragma once
#include "nr_edit_dx12.hpp"
namespace nr_rr_dx12 {
struct alignas(16) Constants {
    unsigned render_size[2]{},output_size[2]{};
    float input_pre_exposure=1,output_pre_exposure=1,new_sample_rate=.08f,depth_absolute_tolerance=.01f;
    float depth_relative_tolerance=.01f,static_motion_tolerance_pixels=1;
    unsigned history_valid=0,frame_pair_valid=0;
    float current_projection_ab[2]{},previous_projection_ab[2]{};
    float current_clip_to_previous_clip[16]{};
    unsigned camera_mapping_verified=0,explicit_previous_depth_verified=0,force_identity=0,padding=0;
};
static_assert(sizeof(Constants)==144);
struct Input {
    nr_private_sr::Recording recording{};
    nr_private_sr::Source source{},motion_reference{};
    nr_edit_dx12::View edited{},original{},motion_pixels{},hardware_depth{},explicit_previous_depth{},clean_output{};
    Constants constants{};
    std::uint64_t view=0,configuration=0,color_domain=0;
    bool scene_linear_verified=false,normalized_guides_verified=false,ordered_queue_verified=false;
    bool camera_cut=true;
    bool allow_spatial_only=false;
};
enum class Status {Invalid,Prepared,Composed,Busy,NativeFailed,Poisoned};
struct Result {Status status=Status::Invalid;bool recorded=false,composed=false;};
class Context;
Context* Create(ID3D12Device*,nr_private_sr::Shape,const std::array<nr_edit_dx12::Bytecode,5>&,
                nr_edit_dx12::SameDevice,std::uint64_t available_bytes,std::uint64_t* reserved_bytes);
Result Begin(Context*,ID3D12GraphicsCommandList*,const Input&);
Result Finish(Context*,ID3D12GraphicsCommandList*,const Input&,std::uint32_t native_rr_result);
void Cancel(Context*);
void SetRetirementGuard(Context*,nr_private_sr::RecordingReleased,void*);
void MarkUncertain(Context*);
bool IsPoisoned(const Context*);
bool Submitted(Context*,nr_private_sr::Recording,ID3D12Fence*,nr_private_sr::Fence,bool success);
void Discarded(Context*,nr_private_sr::Recording);
bool ReadyToRetire(Context*);
bool Retire(Context*&);
}
