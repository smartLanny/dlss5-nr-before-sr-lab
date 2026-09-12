#pragma once
#include "geometry.hpp"
#include "probe_state.hpp"
#include <d3d12.h>
#include <memory>
#include <string>

namespace nr::fullres {
struct Result {
    ID3D12Resource *flow = nullptr;     // RG32_FLOAT, COPY_SOURCE; current -> reference, source pixels
    Frame source{}; // Actual submitted source; never inferred from the next capture.
    std::uint64_t current = 0, reference = 0;
    bool reset = true;
    std::uint32_t grid = 0;
};
struct Delivery {
    ID3D12Resource *output = nullptr; // RGBA8, COPY_SOURCE after Core recording
    ID3D12Fence *completion = nullptr;
    std::uint64_t value = 0;          // Exact private-queue completion value
    Frame source{};
    bool writeback_recorded = false;  // Recording admission, not GPU completion
};
// One in-flight sample, only explicit 100%/50% input sizing and no vendor-to-vendor retry.
// The selected OF backend supplies motion to the process-unique Core V1 owner.
class Session {
public:
    Session(ID3D12Device *device, Backend backend, Extent extent, FlowScale scale = FlowScale::full);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    bool initialized() const noexcept;
    bool idle() const noexcept;
    Progress poll() noexcept; // Bounded wait diagnostics; never releases busy resources.
    bool safe_to_destroy() const noexcept;
    bool faulted() const noexcept;
    const char *status() const noexcept;
    Backend backend() const noexcept;
    Extent extent() const noexcept;
    Extent flow_extent() const noexcept;
    FlowScale scale() const noexcept;
    // Caller records source->snapshot on its OWN known ReShade list. Snapshot
    // enters and leaves each complete sample in COPY_DEST. Never pass a game
    // resource into a private queue. Mark before recording even the first copy.
    ID3D12Resource *snapshot() const noexcept;
    ID3D12Resource *depth_snapshot() const noexcept;
    void mark_capture_recorded() noexcept;
    // Call after real capture Execute/flush and a successful producer Signal.
    // On writeback, Delivery names a later exact private-queue fence; the host
    // must queue a wait before copying output into its effect result texture.
    bool submit(ID3D12Fence *producer, std::uint64_t value, const Frame &frame,
                bool depth_inverted, Delivery &delivery,
                ID3D12Resource *destination = nullptr,
                D3D12_RESOURCE_STATES destination_state = D3D12_RESOURCE_STATE_COPY_SOURCE) noexcept;
    void poison(const char *reason) noexcept;
    bool result(const Frame &view, Result &out) const noexcept; // Same epoch, exact completion; preview only
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace nr::fullres
