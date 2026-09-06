#pragma once
#include <cstdint>

// Private mirror geometry only. This never changes a game's resources, render
// settings, jitter, MV values, or parameter block. No title-specific inference.
namespace nr_mirror_rect {
struct Size { std::uint32_t w = 0, h = 0; };
struct Point { std::uint32_t x = 0, y = 0; };
struct Surface { Size size; Point base; };
enum class Reason {
    None, MissingFlags, IncompleteActive, IncompleteBase,
    InvalidSize, SmallerOutput, ColorBounds, DepthBounds, MotionBounds, OutputBounds
};
struct Plan {
    Reason reason = Reason::InvalidSize;
    Size render, output;
    Point color_base, depth_base, motion_base, output_base;
    bool active_declared = false;
    bool partial_output = false;
    explicit operator bool() const noexcept { return reason == Reason::None; }
};
inline bool Fits(Size size, Point base, Size allocation) noexcept {
    // Subtraction AFTER checking the base avoids UINT_MAX wraparound.
    return size.w && size.h && base.x <= allocation.w && base.y <= allocation.h &&
        size.w <= allocation.w - base.x && size.h <= allocation.h - base.y;
}
inline const char* Name(Reason r) noexcept {
    switch (r) {
    case Reason::None: return "valid";
    case Reason::MissingFlags: return "missing-create-flags";
    case Reason::IncompleteActive: return "incomplete-active-extent";
    case Reason::IncompleteBase: return "incomplete-subrect-base";
    case Reason::InvalidSize: return "invalid-feature-size";
    case Reason::SmallerOutput: return "output-smaller-than-render";
    case Reason::ColorBounds: return "color-subrect-out-of-bounds";
    case Reason::DepthBounds: return "depth-subrect-out-of-bounds";
    case Reason::MotionBounds: return "motion-subrect-out-of-bounds";
    case Reason::OutputBounds: return "output-subrect-out-of-bounds";
    }
    return "unknown";
}
template<class Params>
bool ReadBase(const Params& p, const char* x, const char* y, Point& out) {
    std::uint32_t bx = 0, by = 0;
    const bool hx = p.Get(x, bx), hy = p.Get(y, by);
    if (hx != hy) return false;
    // Both absent is the public NGX default origin. A failed getter's modified
    // output is never used; a half-declared pair is not guessed.
    out = hx ? Point{bx, by} : Point{};
    return true;
}
template<class Params>
Plan Resolve(const Params& p, Size create_render, Size create_output,
             Size color, Size depth, Size motion, Size output) {
    Plan result{};
    result.render = create_render; result.output = create_output;
    std::uint32_t w = 0, h = 0, flags = 0;
    const bool hw = p.Get("DLSS.Render.Subrect.Dimensions.Width", w);
    const bool hh = p.Get("DLSS.Render.Subrect.Dimensions.Height", h);
    if (hw != hh || (hw && ((w == 0) != (h == 0)))) {
        result.reason = Reason::IncompleteActive; return result;
    }
    // Absent or explicit 0/0 denotes the default full input region. Positive
    // Evaluate dimensions are authoritative, even with a larger Create size.
    result.active_declared = hw && w && h;
    if (result.active_declared) result.render = {w, h};
    if (!result.render.w || !result.render.h || !result.output.w || !result.output.h)
        return result;
    if (result.output.w < result.render.w || result.output.h < result.render.h) {
        result.reason = Reason::SmallerOutput; return result;
    }
    if (!p.Get("DLSS.Feature.Create.Flags", flags)) {
        result.reason = Reason::MissingFlags; return result;
    }
    if (!ReadBase(p, "DLSS.Input.Color.Subrect.Base.X", "DLSS.Input.Color.Subrect.Base.Y", result.color_base) ||
        !ReadBase(p, "DLSS.Input.Depth.Subrect.Base.X", "DLSS.Input.Depth.Subrect.Base.Y", result.depth_base) ||
        !ReadBase(p, "DLSS.Input.MV.Subrect.Base.X", "DLSS.Input.MV.Subrect.Base.Y", result.motion_base) ||
        !ReadBase(p, "DLSS.Output.Subrect.Base.X", "DLSS.Output.Subrect.Base.Y", result.output_base)) {
        result.reason = Reason::IncompleteBase; return result;
    }
    const Size motion_region = (flags & 2u) ? result.render : result.output;
    if (!Fits(result.render, result.color_base, color)) result.reason = Reason::ColorBounds;
    else if (!Fits(result.render, result.depth_base, depth)) result.reason = Reason::DepthBounds;
    else if (!Fits(motion_region, result.motion_base, motion)) result.reason = Reason::MotionBounds;
    else if (!Fits(result.output, result.output_base, output)) result.reason = Reason::OutputBounds;
    else result.reason = Reason::None;
    result.partial_output = result.output_base.x || result.output_base.y ||
        result.output.w != output.w || result.output.h != output.h;
    return result;
}
// The source and destination are matching mirrored allocations. Copy exactly
// the NGX output rectangle, at its real base, leaving every other pixel alone.
template<class Context, class Resource, class Box>
void CopyOutput(Context* ctx, Resource* destination, Resource* source, const Plan& p) {
    if (!p) return;
    if (!p.partial_output) { ctx->CopyResource(destination, source); return; }
    Box box{p.output_base.x, p.output_base.y, 0,
            p.output_base.x + p.output.w, p.output_base.y + p.output.h, 1};
    ctx->CopySubresourceRegion(destination, 0, p.output_base.x, p.output_base.y, 0,
                               source, 0, &box);
}
} // namespace nr_mirror_rect
