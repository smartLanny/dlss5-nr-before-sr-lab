#pragma once
#include "nr_edit_dx12.hpp"
namespace nr_deferred_dx12 {
struct Constants {
    unsigned render_size[2]{},output_size[2]{};
    float input_pre_exposure=1,output_pre_exposure=1,carrier_range=4;
    unsigned frame_pair_valid=0,private_sr_succeeded=0,force_identity=0,padding0=0,padding1=0;
};
static_assert(sizeof(Constants)==48);
struct Input {
    nr_private_sr::Frame sr{};
    nr_edit_dx12::View edited{},original{},clean_output{},depth{},motion{};
    Constants constants{};
    std::uint64_t view=0,config_generation=0,color_domain=0;
    bool scene_linear_verified=false,zero_origin_rects_verified=false,ordered_queue_verified=false;
};
enum class Status {Invalid,Ready,Prepared,WaitingPrivateSr,Composed,Busy,Poisoned,NativeFailed};
struct Result {Status status=Status::Invalid;bool recorded=false,composed=false;};
class Context;
Context* Create(ID3D12Device*,nr_private_sr::Api,nr_private_sr::Shape,
                const std::array<nr_edit_dx12::Bytecode,5>&,nr_edit_dx12::SameDevice,
                std::uint64_t available_bytes,std::uint64_t* reserved_bytes);
// Begin records only private edit encoding; original game params remain unchanged.
Result Begin(Context*,ID3D12GraphicsCommandList*,const Input&);
Result Finish(Context*,ID3D12GraphicsCommandList*,const Input&,std::uint32_t native_result);
// CPU invalidation only; recorded resources survive until their exact retirement.
void Cancel(Context*);
void SetRetirementGuard(Context*,nr_private_sr::RecordingReleased,void*);
void MarkUncertain(Context*);
bool IsPoisoned(const Context*);
bool Submitted(Context*,nr_private_sr::Recording,ID3D12Fence*,nr_private_sr::Fence,bool success);
void Discarded(Context*,nr_private_sr::Recording);
bool ReadyToRetire(Context*);
bool Retire(Context*&);
}
