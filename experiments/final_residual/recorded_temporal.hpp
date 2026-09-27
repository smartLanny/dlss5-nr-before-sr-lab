#pragma once
#include "residual_accumulator.hpp"

namespace nr::final_residual {
struct NativeMotion {
    Source source{};
    ID3D12Resource* resource=nullptr;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    unsigned x=0,y=0,width=0,height=0;
    float scale_x=0,scale_y=0; // Current-to-previous displacement, in output pixels.
    bool phase_proven=false;
    ID3D12Resource* depth=nullptr; // Same zero-origin N grid/surface as resource.
    D3D12_RESOURCE_STATES depth_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Source depth_source{};
    bool depth_phase_proven=false;
};
struct RecordedInputs {
    Frame frame{}; // source_time uses monotonic microseconds at the Core boundary.
    Settings settings{};
    unsigned layers=1;
    ID3D12Resource* original=nullptr; // Full active extent, FP16, unenhanced.
    D3D12_RESOURCE_STATES original_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    ID3D12Resource* candidate=nullptr; // Current composed Z; updated only after all admission checks.
    DXGI_FORMAT candidate_format=DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES candidate_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    unsigned candidate_x=0,candidate_y=0;
    Source anchor_source{}; // Set only after Core composed NR1 into anchor().
    NativeMotion native{};
};
enum class RecordResult { Disabled, Unavailable, Duplicate, Busy, Recorded, Faulted };
struct TemporalStats {
    Id recorded=0,submitted=0,cancelled=0,seeded=0,bytes=0;
    Id pending_token=0;
    float weight=0;
    bool faulted=false;
    Id snapshot_bytes=0,inplace_recorded=0,snapshot_recorded=0;
    bool inplace_available=false; // Availability/recording are not GPU completion.
    Id gpu_configuration=0,gpu_owner=0; // Completed recording identity, never current global configuration.
    Id gpu_frames=0,gpu_tested=0,gpu_adopted=0,gpu_rejected=0,gpu_warmup=0,gpu_depth_rejected=0;
    bool snapshot_refused=false; // Allocation/budget refusal; current NR is retained.
};

// One serialized Core context; all work and final consumers use the proven
// constructor queue. No production Execute/Signal/Wait occurs here. Completed GPU statistics use nonblocking readback only after their exact fence.
// Recorded is NOT submitted/adopted/completed. Core supplies the actual last
// consumer's fence only after its Execute+Signal hook succeeds.
class RecordedTemporal {
public:
    RecordedTemporal(ID3D12Device*,ID3D12CommandQueue*,unsigned,unsigned,
        Backend,Scope,Id own_texture_budget);
    ~RecordedTemporal();
    RecordedTemporal(const RecordedTemporal&)=delete;
    RecordedTemporal& operator=(const RecordedTemporal&)=delete;
    bool initialized() const noexcept;
    const char* status() const noexcept;
    bool can_record() const noexcept;
    // All of this instance's recordings must be non-replayable and its final
    // consumers complete. Unrelated NR recordings do not own these resources.
    bool releasable() const noexcept;
    ID3D12Resource* anchor() const noexcept; // FP16, UAV; only allocated for LaterLayers.
    RecordResult record(ID3D12GraphicsCommandList*,const RecordedInputs&) noexcept;
    bool submitted(ID3D12Fence* exact_last_consumer,Id value,Id recording_lease=0) noexcept;
    // Serialized module-level replay contract: keep the recording lease until
    // this actual Execute+Signal receipt is supplied, then end_recording after
    // Reset. No concurrent Reset/receipt ordering is inferred here. Full Core
    // independently quarantines opaque NR replay before this callback.
    bool resubmitted(Id recording_lease,ID3D12Fence*,Id value) noexcept;
    void end_recording(Id recording_lease) noexcept; // Core proved the list can no longer replay.
    bool cancelled_unsubmitted() noexcept; // Caller proved successful Reset, no Execute.
    TemporalStats stats() const noexcept;
    static RetirementStats collect_retired() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
