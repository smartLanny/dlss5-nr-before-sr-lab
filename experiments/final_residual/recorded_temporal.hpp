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
};

// One serialized Core context; all work and final consumers use the proven
// constructor queue. No production Execute/Signal/Wait/Map occurs here.
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
    ID3D12Resource* anchor() const noexcept; // FP16, UAV; only allocated for LaterLayers.
    RecordResult record(ID3D12GraphicsCommandList*,const RecordedInputs&) noexcept;
    bool submitted(ID3D12Fence* exact_last_consumer,Id value,Id recording_lease=0) noexcept;
    void end_recording(Id recording_lease) noexcept; // Core proved the list can no longer replay.
    bool cancelled_unsubmitted() noexcept; // Caller proved successful Reset, no Execute.
    TemporalStats stats() const noexcept;
    static RetirementStats collect_retired() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
