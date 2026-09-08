#pragma once
#include "nr_fsr31_dlss.h"
#include "nr_external_provider.h"

// A real FFX dispatch -> the existing NR ABI, before direct SR. This does not
// intercept a game API or allocate/submit work. Source provenance is explicit;
// synthetic fixtures cannot silently turn into measured game input.
namespace nr_fsr31_external {
struct Lease {
    std::uint64_t provider_id = 0, view_id = 0, epoch = 0, frame_id = 0;
    ID3D12CommandQueue *queue = nullptr;
    ID3D12Fence *fence = nullptr;
    std::uint64_t last_completed_submission = 0;
    NrGuideProvenance provenance = NrGuideProvenance::Unknown;
    bool command_list_owned = false; // Explicit caller lease; not inferred from a pointer.
};

inline nr_fsr31_dlss::Status BuildFrame(ID3D12Device *device,
        const ffxCreateContextDescUpscale &create,
        const ffxDispatchDescUpscale &dispatch,
        const nr_fsr31_dlss::Quality &quality, ID3D12Resource *nr_output,
        const Lease &lease, NrExternalFrameV1 &output) {
    using S = nr_fsr31_dlss::Status;
    output = {};
    nr_fsr31_dlss::Mapping checked{};
    const auto mapped = nr_fsr31_dlss::Map(device, create, dispatch, checked, quality);
    if (mapped != S::Ready) return mapped;
    if (!lease.provider_id || !lease.view_id || !lease.epoch || !lease.frame_id ||
        !lease.queue || !lease.fence || !nr_output) return S::MissingInput;
    if (!lease.command_list_owned) return S::UnsupportedState;
    if (lease.provenance != NrGuideProvenance::Native &&
        lease.provenance != NrGuideProvenance::Extracted &&
        lease.provenance != NrGuideProvenance::Synthetic) return S::UnsupportedFlags;
    // These are the current consumer's actual limits, not the broader SR map.
    // Reject before any recording: no cropped allocation, high-res MV or
    // exposure texture can be silently reinterpreted as this same-grid lease.
    if ((create.flags & FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) ||
        dispatch.exposure.resource) return S::UnsupportedResource;
    const unsigned rw = dispatch.renderSize.width, rh = dispatch.renderSize.height;
    for (const auto *view : {&dispatch.color, &dispatch.depth, &dispatch.motionVectors})
        if (view->description.width != rw || view->description.height != rh)
            return S::InvalidExtent;
    if (nr_output == dispatch.color.resource || nr_output == dispatch.depth.resource ||
        nr_output == dispatch.motionVectors.resource || nr_output == dispatch.output.resource)
        return S::AliasedOutput;
    Microsoft::WRL::ComPtr<ID3D12Device> owner;
    if (lease.queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
        FAILED(lease.queue->GetDevice(IID_PPV_ARGS(&owner))) ||
        !nr_fsr31_dlss::SameDevice(device, owner.Get())) return S::DeviceMismatch;
    owner.Reset();
    if (FAILED(lease.fence->GetDevice(IID_PPV_ARGS(&owner))) ||
        !nr_fsr31_dlss::SameDevice(device, owner.Get())) return S::DeviceMismatch;
    const auto completed = lease.fence->GetCompletedValue();
    if (completed == std::numeric_limits<std::uint64_t>::max() ||
        completed < lease.last_completed_submission) return S::UnsupportedState;
    const auto nr_view = ffxApiGetResourceDX12(nr_output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto valid_output = nr_fsr31_dlss::Resource(device, nr_view, rw, rh,
        DXGI_FORMAT_R16G16B16A16_FLOAT, FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT, true);
    if (valid_output != S::Ready) return valid_output;

    NrExternalFrameV1 next{};
    next.provider_id = lease.provider_id; next.view_id = lease.view_id;
    next.epoch = lease.epoch; next.frame_id = lease.frame_id;
    next.command_list = reinterpret_cast<std::uint64_t>(dispatch.commandList);
    next.queue = reinterpret_cast<std::uint64_t>(lease.queue);
    next.fence = reinterpret_cast<std::uint64_t>(lease.fence);
    next.fence_value = lease.last_completed_submission;
    next.pre_exposure = dispatch.preExposure;
    next.lease_flags = static_cast<unsigned>(NrExternalLeaseFlags::CommandListOwnedByProducer) |
        static_cast<unsigned>(NrExternalLeaseFlags::QueueFenceRequired);
    if (dispatch.reset) next.lease_flags |= static_cast<unsigned>(NrExternalLeaseFlags::ResetTemporal);
    if (create.flags & FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE)
        next.lease_flags |= static_cast<unsigned>(NrExternalLeaseFlags::HdrColor);
    if (create.flags & FFX_UPSCALE_ENABLE_DEPTH_INVERTED)
        next.lease_flags |= static_cast<unsigned>(NrExternalLeaseFlags::DepthInverted);
    auto fill = [&](const FfxApiResource &view, NrExternalResourceV1 &r) {
        auto *resource = static_cast<ID3D12Resource *>(view.resource);
        const auto desc = resource->GetDesc();
        r.resource = reinterpret_cast<std::uint64_t>(resource);
        r.format = desc.Format; r.width = r.rect_w = rw; r.height = r.rect_h = rh;
        r.state_before = r.state_after = view.state == FFX_API_RESOURCE_STATE_UNORDERED_ACCESS
            ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
            : view.state == FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ
                ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        r.provenance = lease.provenance;
        r.jitter_x = dispatch.jitterOffset.x; r.jitter_y = dispatch.jitterOffset.y;
    };
    fill(dispatch.color, next.color); fill(dispatch.depth, next.depth);
    fill(dispatch.motionVectors, next.motion); fill(nr_view, next.output);
    next.motion.motion_scale_x = dispatch.motionVectorScale.x;
    next.motion.motion_scale_y = dispatch.motionVectorScale.y;
    if (!nr_external_provider::FrameHeaderOk(next) || !nr_external_provider::ValidateLeases(next))
        return S::UnsupportedResource;
    output = next;
    return S::Ready;
}
} // namespace nr_fsr31_external
