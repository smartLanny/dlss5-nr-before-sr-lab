#pragma once
#include "history_contract.hpp"
#include <d3d12.h>
#include <memory>

namespace nr::final_residual {
enum class Submit { Rejected, Duplicate, Busy, Submitted, Faulted };
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

// Standalone actual FFX1.1.4 producer; no Core/ExternalProvider/NGX dependency.
// One serialized owner and one outstanding result. The caller's DIRECT queue
// orders immutable private source snapshots, dispatch and output consumers.
// No game-owned resource or cross-queue source is accepted by this contract.
class AmdOfProducer {
public:
    AmdOfProducer(ID3D12Device*,ID3D12CommandQueue*,unsigned width,unsigned height,unsigned percent=100);
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
    Id dispatch_count() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
