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
// One in-flight sample, only explicit 100%/50% input sizing and no vendor-to-vendor retry.
// This probe does NOT call Feature18, SR, FG, or replace the existing Core.
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
    void mark_capture_recorded() noexcept;
    // Call after real Execute/flush and a successful producer Signal. That
    // producer signal also covers any preceding output->ReShade copy.
    bool submit(ID3D12Fence *producer, std::uint64_t value, const Frame &frame) noexcept;
    void poison(const char *reason) noexcept;
    bool result(const Frame &view, Result &out) const noexcept; // Same epoch, exact completion; preview only
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace nr::fullres
