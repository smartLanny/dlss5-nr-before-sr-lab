#pragma once
#include "amd_of_producer.hpp"

namespace nr::final_residual {
// Explicit experiment domain: SDR sRGB code values in [0,1], no HDR conversion.
constexpr Id kResidualSdrCodeValues=1;
enum class ResidualSubmit { Disabled, Rejected, Duplicate, Busy, Submitted, Faulted };
struct ResidualInputs {
    Source original_source{}, enhanced_source{};
    ID3D12Resource* original=nullptr; // immutable RGBA8_UNORM, COPY_SOURCE
    ID3D12Resource* enhanced=nullptr; // final chain result RGBA16_FLOAT, COPY_SOURCE
};
struct ResidualOutput {
    Id token=0, history_generation=0;
    ID3D12Resource* pixels=nullptr; // RGBA16_FLOAT final SDR, COPY_SOURCE
    ID3D12Resource* signed_history=nullptr; // RGBA16_FLOAT signed residual, COPY_SOURCE
    ID3D12Fence* completion=nullptr;
    Id value=0;
    bool accumulated=false;
};
// Original project shader/policy, not Magpie code. Single serialized owner.
// Inputs and flow must be immutable private resources ordered on this exact
// same-device DIRECT queue. Caller owns HistoryContract and passes its Plan.
// Adoption is legal only after Submitted; producer output can be released only
// after this consumer's completion. A returned output remains occupied until
// the caller supplies the actual last reader's completed fence.
class ResidualAccumulator {
public:
    ResidualAccumulator(ID3D12Device*,ID3D12CommandQueue*,unsigned width,unsigned height);
    ~ResidualAccumulator();
    ResidualAccumulator(const ResidualAccumulator&)=delete;
    ResidualAccumulator& operator=(const ResidualAccumulator&)=delete;
    bool initialized() const noexcept;
    const char* status() const noexcept;
    ResidualSubmit submit(bool enabled,const Frame&,const Plan&,const GpuFlow&,const ResidualInputs&) noexcept;
    bool poll(ResidualOutput&) noexcept;
    bool release_output(Id token,ID3D12Fence* last_reader,Id value) noexcept;
    bool begin_retirement(Id token,ID3D12Fence* last_reader,Id value) noexcept;
    static RetirementStats collect_retired() noexcept;
    Id dispatch_count() const noexcept;
    Id allocated_texture_bytes() const noexcept; // actual texture allocation sizes, excludes PSO/driver overhead
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
