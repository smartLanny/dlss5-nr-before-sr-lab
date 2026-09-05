#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>

// Metadata-only admission boundary. Passing this validator does NOT prove GPU
// synchronization or dispatch DLSS. The D3D12 observer must supply the evidence.
namespace nr::research::temporal {
struct Extent { std::uint32_t width = 0, height = 0; };
struct Rect { std::uint32_t x = 0, y = 0, width = 0, height = 0; };
inline bool Same(Extent a, Extent b) { return a.width == b.width && a.height == b.height; }
inline bool Valid(Extent e) { return e.width != 0 && e.height != 0; }
inline bool Fits(Rect r, Extent e) {
    return Valid(e) && r.width && r.height && r.x <= e.width && r.y <= e.height &&
        r.width <= e.width - r.x && r.height <= e.height - r.y;
}
struct Resource {
    Extent physical;
    Rect active;
    std::uint64_t frame = 0, adapter = 0;
    bool typedViewVerified = false, stateAndLifetimeVerified = false;
};
enum class Api { Unknown, D3D12, D3D11, Vulkan };
enum class Origin { Unknown, NativeDlss, Fsr31, ExtractedTaa, Synthetic };
enum class Reject {
    None, Disabled, ProtectedSession, OtherOwner, RayReconstruction,
    ApiUnsupported, IdentityUnknown, ExtentInvalid, ResourceInvalid,
    MissingSemantics, MotionJittered, ExposureUnsupported, GridInvalid,
    VersionUnsupported, SubrectUnsupported
};
struct Frame {
    Api api = Api::Unknown;
    Origin origin = Origin::Unknown;
    Extent render, output;
    Resource color, depth, motion, destination;
    std::uint64_t frame = 0, adapter = 0;
    float motionToRenderX = 0, motionToRenderY = 0;
    float jitterX = 0, jitterY = 0, preExposure = 0;
    bool enabled = false, offlineAllowed = false, otherOwner = false, rr = false;
    bool nativeFrame = false, queueVerified = false;
    bool linearColorVerified = false, depthConventionVerified = false;
    bool motionConventionVerified = false, motionJittered = false;
    bool motionAtOutput = false, exposureVerified = false, reset = false;
};
inline bool ResourceValid(const Resource& r, const Frame& f, Extent active) {
    return Fits(r.active, r.physical) && Same({r.active.width, r.active.height}, active) &&
        r.frame == f.frame && r.adapter == f.adapter && r.typedViewVerified &&
        r.stateAndLifetimeVerified;
}
inline Reject Validate(const Frame& f) {
    if (!f.enabled) return Reject::Disabled;
    if (!f.offlineAllowed) return Reject::ProtectedSession;
    if (f.otherOwner) return Reject::OtherOwner;
    if (f.rr) return Reject::RayReconstruction;
    if (f.api != Api::D3D12) return Reject::ApiUnsupported;
    if (!f.frame || !f.adapter || !f.nativeFrame || !f.queueVerified)
        return Reject::IdentityUnknown;
    if (!Valid(f.render) || !Valid(f.output) || f.render.width > f.output.width ||
        f.render.height > f.output.height) return Reject::ExtentInvalid;
    if (!ResourceValid(f.color, f, f.render) || !ResourceValid(f.depth, f, f.render) ||
        !ResourceValid(f.motion, f, f.motionAtOutput ? f.output : f.render) ||
        !ResourceValid(f.destination, f, f.output)) return Reject::ResourceInvalid;
    if (f.origin == Origin::Unknown || !f.linearColorVerified ||
        !f.depthConventionVerified || !f.motionConventionVerified ||
        !std::isfinite(f.jitterX) || !std::isfinite(f.jitterY) ||
        !std::isfinite(f.motionToRenderX) || !std::isfinite(f.motionToRenderY) ||
        f.motionToRenderX == 0 || f.motionToRenderY == 0) return Reject::MissingSemantics;
    if (f.motionJittered) return Reject::MotionJittered;
    if (!f.exposureVerified || !std::isfinite(f.preExposure) || f.preExposure <= 0)
        return Reject::ExposureUnsupported;
    return Reject::None;
}
struct Grid {
    Extent work;
    float motionX = 0, motionY = 0, jitterX = 0, jitterY = 0;
};
inline Reject MapToNr(const Frame& f, Extent work, Grid& out) {
    out = {};
    const auto rejected = Validate(f);
    if (rejected != Reject::None) return rejected;
    if (!Valid(work) || work.width > f.render.width || work.height > f.render.height)
        return Reject::GridInvalid;
    const float x = static_cast<float>(work.width) / static_cast<float>(f.render.width);
    const float y = static_cast<float>(work.height) / static_cast<float>(f.render.height);
    out = {work, f.motionToRenderX * x, f.motionToRenderY * y, f.jitterX * x, f.jitterY * y};
    return Reject::None;
}
// FSR 3.1 observation, NOT a binary SDK struct. No reinterpret_cast ABI guesses.
// Initial carrier requires explicit exposure and unjittered render-resolution MV.
// Display-resolution MV and jitter cancellation need separately verified adapters.
struct Fsr31Observation {
    Frame frame;
    std::uint32_t apiMajor = 0, apiMinor = 0;
    float motionVectorScaleX = 0, motionVectorScaleY = 0;
    bool autoExposure = false;
};
inline Reject NormalizeFsr31(const Fsr31Observation& in, Frame& out) {
    out = {};
    if (in.apiMajor != 3 || in.apiMinor != 1) return Reject::VersionUnsupported;
    if (in.autoExposure) return Reject::ExposureUnsupported;
    if (in.frame.motionAtOutput) return Reject::MissingSemantics;
    // ffxDispatchDescUpscale has renderSize, not arbitrary input subrect bases.
    for (const Resource* r : {&in.frame.color, &in.frame.depth, &in.frame.motion, &in.frame.destination})
        if (r->active.x || r->active.y) return Reject::SubrectUnsupported;
    Frame candidate = in.frame;
    candidate.origin = Origin::Fsr31;
    candidate.motionToRenderX = in.motionVectorScaleX;
    candidate.motionToRenderY = in.motionVectorScaleY;
    const auto rejected = Validate(candidate);
    if (rejected == Reject::None) out = candidate;
    return rejected;
}
} // namespace nr::research::temporal
