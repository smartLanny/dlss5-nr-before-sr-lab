#pragma once
#include "history_contract.hpp"
#include <d3d12.h>
#include <memory>

namespace nr::final_residual {
enum class Submit { Rejected, Duplicate, Busy, Submitted, Faulted, Recorded };
struct GpuFlow {
    FlowReceipt receipt{};
    Id token=0;
    ID3D12Resource* vectors=nullptr; // RG32_FLOAT, source extent, COPY_SOURCE
    ID3D12Resource* scene_data=nullptr; // SDK's 3 x 1 R32_UINT, COPY_SOURCE; not a confidence map
    ID3D12Fence* completion=nullptr;
    Id value=0;
    unsigned frames_since_reset=0,scene_history_bits=0;
    float scene_change=0; // SDK histogram divergence, not per-pixel confidence.
};
struct RetirementStats {unsigned limit=4,active=0,pending=0,quarantined=0;};
struct RecordedFlow {
    Source current{},reference{};
    Id token=0,history_generation=0;
    ID3D12Resource* grid=nullptr; // R16G16_SINT, ceil(flow extent / 8), COMPUTE_READ.
    ID3D12Resource* scene_data=nullptr; // 3 x 1 R32_UINT; GPU tests SCD and finite score.
    unsigned flow_width=0,flow_height=0,frames_since_reset=0;
    bool reset=false;
};

// Standalone actual FFX1.1.4 producer; no Core/ExternalProvider/NGX dependency.
// One serialized owner and one outstanding result. The caller's DIRECT queue
// orders immutable private source snapshots, dispatch and output consumers.
// No game-owned resource or cross-queue source is accepted by this contract.
class AmdOfProducer {
public:
    AmdOfProducer(ID3D12Device*,ID3D12CommandQueue*,unsigned width,unsigned height,unsigned percent=100,bool borrowed=false);
    ~AmdOfProducer();
    AmdOfProducer(const AmdOfProducer&)=delete;
    AmdOfProducer& operator=(const AmdOfProducer&)=delete;
    bool initialized() const noexcept;
    const char* status() const noexcept;
    // Snapshot: immutable original SDR RGBA8, single subresource, COPY_SOURCE.
    // Must already be ordered on the constructor queue; retained until release.
    Submit dispatch(const Frame&,const Plan&,ID3D12Resource* private_snapshot) noexcept;
    bool poll(GpuFlow&) noexcept; // Ready only after the exact producer fence.
    // After all actual output reads are submitted, pass their exact completion.
    // Fence must be same-device and already complete; false never releases.
    bool release_output(Id token,ID3D12Fence* consumer_completion,Id value) noexcept;
    // Terminal handoff after the LAST consumer was submitted on the owner
    // queue. Unlike release_output this may be pending. No future dispatch or
    // consumer is permitted after success; a token is not a completion fence.
    bool begin_retirement(Id token,ID3D12Fence* consumer_completion,Id value) noexcept;
    // Production seam: records on the caller's proven ordered DIRECT list.
    // It never closes/executes the list, signals, maps SCD, or waits on the CPU.
    // A true result has GPU-readable vectors/SCD, NOT a completed FlowReceipt.
    // commit_recording is called only after actual owner Execute+Signal success;
    // cancel_recording requires proof that the unsubmitted recording was reset.
    bool can_record() const noexcept;
    Submit record(const Frame&,const Plan&,ID3D12GraphicsCommandList*,ID3D12Resource*,
        D3D12_RESOURCE_STATES snapshot_state,RecordedFlow&) noexcept;
    bool commit_recording(Id token,ID3D12Fence* last_reader,Id value) noexcept;
    bool cancel_recording(Id token) noexcept;
    // Configuration/shutdown owner calls this outside rendering locks. Never
    // waits for GPU. A maximum of four active/retired contexts can own resources;
    // unresolved Signal, consumer or device-loss evidence occupies its slot.
    static RetirementStats collect_retired() noexcept;
    Id dispatch_count() const noexcept;
    Id gpu_allocation_bytes() const noexcept; // Own textures + SDK-reported allocations; excludes driver overhead.
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
