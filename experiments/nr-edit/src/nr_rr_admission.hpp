#pragma once
#include "nr_rr_dx12.hpp"
#include <cmath>

// CPU-only prerequisites shared by the experimental RR edit recorder and its
// contract probe. This does not assert that a Feature 13 or material producer exists.
namespace nr_rr_dx12 {
enum class Admission : unsigned {
    Accepted, Recording, Source, Domain, Guides, Extent, FramePair,
    Identity, Exposure, SampleRate, DepthTolerance, MotionTolerance,
    PreviousDepth, Reprojection
};
inline Admission AdmitScalars(const Input& i, const nr_private_sr::Shape& s) noexcept {
    const auto& k=i.constants;
    if(!i.recording || i.recording.device!=s.device || i.recording.queue!=s.queue)
        return Admission::Recording;
    if(!i.source || !i.view || !i.configuration || !i.color_domain)
        return Admission::Source;
    if(!i.scene_linear_verified) return Admission::Domain;
    if(!i.normalized_guides_verified || !i.ordered_queue_verified)
        return Admission::Guides;
    if(k.render_size[0]!=s.render_width || k.render_size[1]!=s.render_height ||
       k.output_size[0]!=s.output_width || k.output_size[1]!=s.output_height)
        return Admission::Extent;
    if(!k.frame_pair_valid) return Admission::FramePair;
    if(k.force_identity) return Admission::Identity;
    if(!std::isfinite(k.input_pre_exposure) || k.input_pre_exposure<=1e-6f ||
       !std::isfinite(k.output_pre_exposure) || k.output_pre_exposure<=1e-6f)
        return Admission::Exposure;
    if(!std::isfinite(k.new_sample_rate) || k.new_sample_rate<=0 || k.new_sample_rate>1)
        return Admission::SampleRate;
    if(!std::isfinite(k.depth_absolute_tolerance) || k.depth_absolute_tolerance<0 ||
       !std::isfinite(k.depth_relative_tolerance) || k.depth_relative_tolerance<0)
        return Admission::DepthTolerance;
    if(!std::isfinite(k.static_motion_tolerance_pixels) || k.static_motion_tolerance_pixels<0)
        return Admission::MotionTolerance;
    if(k.explicit_previous_depth_verified && !i.explicit_previous_depth.resource)
        return Admission::PreviousDepth;
    if(!k.camera_mapping_verified && !k.explicit_previous_depth_verified && !i.allow_spatial_only)
        return Admission::Reprojection;
    for(float v:k.current_projection_ab) if(!std::isfinite(v)) return Admission::Reprojection;
    if(k.camera_mapping_verified)
        for(float v:k.current_clip_to_previous_clip)
            if(!std::isfinite(v)) return Admission::Reprojection;
    return Admission::Accepted;
}
}
