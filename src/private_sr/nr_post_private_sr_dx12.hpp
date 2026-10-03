#pragma once
// Owned post-NR residual transport. No game hooks, native Evaluate, or NGX Init.
#include "nr_post_residual_dx12.hpp"

namespace nr_post_private_sr_dx12 {
struct Input {
    nr_post_residual_dx12::Pair pair{};
    nr_edit_dx12::View target{}, depth{}, motion{};
    nr_post_residual_dx12::Rect target_rect{};
    nr_private_sr::Recording recording{};
    nr_private_sr::Source guide_source{}, motion_reference{};
    std::uint64_t view=0, configuration=0, encoding_generation=0;
    // The producer, not texture size, proves N-grid motion units and post-SR
    // phase. This backend does not fabricate native provenance from NGX fields.
    bool target_contains_candidate=false;
    bool ordered_queue_verified=false, post_sr_phase_verified=false;
    bool normalized_guide_units_verified=false, reset=true;
    float motion_scale_x=1, motion_scale_y=1;
    unsigned native_result=0;
};
enum class Status { InvalidContract, Busy, WaitingForSr, Composed, Poisoned };
struct Result {
    Status status=Status::InvalidContract;
    bool recorded=false, composed=false;
    unsigned sr_result=0, slot=0;
    nr_private_sr::BindEvidence bind{};
};
class Context;
// Diagnostic view for a still-live, exact recording. Borrowed resources must
// be copied on that recording before submission; callers never retain them.
struct RecordedPlanes {
    ID3D12Resource* carrier=nullptr;
    ID3D12Resource* tail=nullptr;
    ID3D12Resource* reconstructed=nullptr;
    explicit operator bool() const {return carrier&&tail&&reconstructed;}
};
RecordedPlanes InspectRecordedPlanes(const Context*,unsigned slot,nr_private_sr::Recording);
RecordedPlanes InspectRecordedPlanes(const Context*,nr_private_sr::Recording);
// Development-host replay of the exact encoded carrier/tail through the same
// production Compose shader. Only a live, unsubmitted recording is accepted;
// target must already contain this frame's Z. This never calls Feature1.
bool RecordSpatialReference(Context*,ID3D12GraphicsCommandList*,unsigned slot,
                            nr_private_sr::Recording,nr_edit_dx12::View spatial,
                            nr_edit_dx12::View target,nr_post_residual_dx12::Rect target_rect);
// Three independent scratch slots, one private Feature1 history. The existing
// caller serializes CPU access and supplies verified original API ownership.
// accounted_plane_bytes excludes opaque NGX memory and descriptor/kernel storage.
Context* Create(ID3D12Device*, nr_private_sr::Api, nr_private_sr::Shape,
                const std::array<nr_edit_dx12::Bytecode,static_cast<unsigned>(nr_edit_dx12::Kernel::Count)>&,
                nr_edit_dx12::SameDevice, std::uint64_t available_plane_bytes,
                std::uint64_t* accounted_plane_bytes);
// B/Z are already the same-frame native baseline/final controlled NR candidate.
// Only this method can mark reconstructed residuals successful after its actual
// private SR Evaluate; callers cannot provide an arbitrary success flag/output.
// No caller image is written unless composition is recorded. Failure preserves Z.
Result Record(Context*, ID3D12GraphicsCommandList*, const Input&);
bool Submitted(Context*, nr_private_sr::Recording, ID3D12Fence*, nr_private_sr::Fence,
               bool execute_and_signal_succeeded);
void Discarded(Context*, nr_private_sr::Recording);
void Cancel(Context*);
bool Collect(Context*);
bool Poisoned(const Context*);
bool ReadyToRetire(Context*);
bool Retire(Context*&);
}
