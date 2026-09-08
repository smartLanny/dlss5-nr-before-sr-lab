#pragma once
#include "nr_external_provider_abi.h"

// Maps the D3D12 same-device Feeder FeedFrame12 contract onto NrExternalFrameV1.
// Derived from jlrouzies-fr/DLSS5-Feeder production FeedFrame12 (bb/mv/depth,
// Output writeback home). Does not create NGX Feature1 handles.
namespace nr_external_provider_feeder {

struct FeedInputs {
    std::uint64_t command_list = 0;
    std::uint64_t color = 0;   // snapshot of backbuffer / private color
    std::uint64_t output = 0;  // SLOT_OUTPUT (UAV) or backbuffer when in-place
    std::uint64_t depth = 0;   // ReShade depth texture (zero-copy)
    std::uint64_t motion = 0;  // estimated MV provider texture
    std::uint64_t exposure = 0;
    std::uint64_t mask = 0;    // optional; unused by MC1 writeback
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t color_format = 0;
    std::uint32_t depth_format = 0;
    std::uint32_t motion_format = 0;
    std::uint32_t exposure_format = 0;
    std::uint32_t color_state = 0;
    std::uint32_t output_state = 0;
    std::uint32_t depth_state = 0;
    std::uint32_t motion_state = 0;
    std::uint32_t color_subresource = 0;
    std::uint32_t output_subresource = 0;
    std::uint32_t depth_subresource = 0;
    std::uint32_t motion_subresource = 0;
    std::uint32_t rect_x = 0;
    std::uint32_t rect_y = 0;
    std::uint32_t rect_w = 0; // 0 => full width
    std::uint32_t rect_h = 0;
    float mv_scale_x = 1.0f;
    float mv_scale_y = 1.0f;
    float pre_exposure = 1.0f;
    float exposure_scale = 1.0f;
    float jitter_x = 0.0f;
    float jitter_y = 0.0f;
    bool reset = false;
    bool output_is_color = false;
    bool depth_inverted = false;
    bool hdr_color = false;
    std::uint64_t view_id = 0;
    std::uint64_t epoch = 0;
    std::uint64_t frame_id = 0;
    std::uint64_t provider_id = kNrExternalProviderIdDl5f;
    std::uint64_t queue = 0;
    std::uint64_t fence = 0;
    std::uint64_t fence_value = 0;
};

inline bool BuildSyntheticFrame(const FeedInputs &in, NrExternalFrameV1 &out) noexcept {
    if (!in.command_list || !in.color || !in.output || !in.width || !in.height ||
        !in.view_id || !in.epoch || !in.frame_id || !in.queue || !in.fence)
        return false;
    const std::uint32_t rw = in.rect_w ? in.rect_w : in.width;
    const std::uint32_t rh = in.rect_h ? in.rect_h : in.height;
    out = NrExternalFrameV1{};
    out.provider_id = in.provider_id ? in.provider_id : kNrExternalProviderIdDl5f;
    out.view_id = in.view_id;
    out.epoch = in.epoch;
    out.frame_id = in.frame_id;
    out.lease_flags = static_cast<std::uint32_t>(NrExternalLeaseFlags::CommandListOwnedByProducer);
    if (in.output_is_color || in.color == in.output)
        out.lease_flags |= static_cast<std::uint32_t>(NrExternalLeaseFlags::OutputSameAsColor);
    if (in.reset)
        out.lease_flags |= static_cast<std::uint32_t>(NrExternalLeaseFlags::ResetTemporal);
    if (in.depth_inverted)
        out.lease_flags |= static_cast<std::uint32_t>(NrExternalLeaseFlags::DepthInverted);
    if (in.hdr_color)
        out.lease_flags |= static_cast<std::uint32_t>(NrExternalLeaseFlags::HdrColor);
    if (in.queue && in.fence)
        out.lease_flags |= static_cast<std::uint32_t>(NrExternalLeaseFlags::QueueFenceRequired);
    out.command_list = in.command_list;
    out.queue = in.queue;
    out.fence = in.fence;
    out.fence_value = in.fence_value;
    out.pre_exposure = in.pre_exposure;
    out.exposure_scale = in.exposure_scale;

    auto fill = [&](NrExternalResourceV1 &r, std::uint64_t resource,
                    std::uint32_t format, std::uint32_t state,
                    std::uint32_t subresource, NrGuideProvenance provenance) {
        r.resource = resource;
        r.subresource = subresource;
        r.format = format;
        r.width = in.width;
        r.height = in.height;
        r.rect_x = in.rect_x;
        r.rect_y = in.rect_y;
        r.rect_w = rw;
        r.rect_h = rh;
        r.state_before = state;
        r.state_after = state;
        r.provenance = provenance;
        r.jitter_x = in.jitter_x;
        r.jitter_y = in.jitter_y;
    };
    fill(out.color, in.color, in.color_format, in.color_state,
         in.color_subresource, NrGuideProvenance::Synthetic);
    fill(out.output, in.output, in.color_format, in.output_state,
         in.output_subresource, NrGuideProvenance::Synthetic);
    if (in.depth) {
        fill(out.depth, in.depth, in.depth_format, in.depth_state,
             in.depth_subresource, NrGuideProvenance::Synthetic);
    }
    if (in.motion) {
        fill(out.motion, in.motion, in.motion_format, in.motion_state,
             in.motion_subresource, NrGuideProvenance::Synthetic);
        out.motion.motion_unit = NrExternalMotionUnit::Pixels;
        out.motion.motion_scale_x = in.mv_scale_x;
        out.motion.motion_scale_y = in.mv_scale_y;
    }
    if (in.exposure) {
        fill(out.exposure, in.exposure, in.exposure_format, 0, 0,
             NrGuideProvenance::Synthetic);
        out.exposure.width = 1;
        out.exposure.height = 1;
        out.exposure.rect_w = 1;
        out.exposure.rect_h = 1;
    }
    (void)in.mask;
    return true;
}

} // namespace nr_external_provider_feeder
