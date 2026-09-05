#pragma once
#include <cstdint>

namespace nr_render_scale {
struct Extent { std::uint32_t w = 0, h = 0; };
struct Bounds { Extent min, max; };
enum class Decision { Disabled, Invalid, UnsupportedRange, Accepted };
struct Plan { Decision decision = Decision::Disabled; Extent render; };

// Percent is linear, NOT pixel area. This policy only answers a real engine
// optimal-settings query, never edits an Evaluate extent or shrinks a texture.
inline Plan Negotiate(std::uint32_t percent, Extent output, Bounds bounds) {
    if (!percent) return {};
    const auto valid = [](Extent e) {
        return e.w >= 64 && e.h >= 64 && e.w <= 32768 && e.h <= 32768;
    };
    if (percent < 50 || percent > 100 || !valid(output) ||
        !valid(bounds.min) || !valid(bounds.max) ||
        bounds.min.w > bounds.max.w || bounds.min.h > bounds.max.h)
        return {Decision::Invalid, {}};
    // Nearest even dimensions; avoid overflow, NaN, aspect-ratio distortion.
    const auto scaled = [percent](std::uint32_t v) {
        return static_cast<std::uint32_t>((std::uint64_t(v) * percent + 100) / 200 * 2);
    };
    Extent r{scaled(output.w), scaled(output.h)};
    if (r.w < bounds.min.w || r.h < bounds.min.h ||
        r.w > bounds.max.w || r.h > bounds.max.h)
        return {Decision::UnsupportedRange, r};
    return {Decision::Accepted, r};
}

// Adapter deliberately exposes only uint operations, for both production ABI
// and CPU tests. All reads complete before either of the two output writes.
template<class Params>
inline Plan ApplyOptimal(Params& p, std::uint32_t percent, bool safe) {
    if (!percent || !safe) return {};
    Extent output{}; Bounds bounds{};
    if (!p.Get("Width", output.w) || !p.Get("Height", output.h) ||
        !p.Get("DLSS.Get.Dynamic.Min.Render.Width", bounds.min.w) ||
        !p.Get("DLSS.Get.Dynamic.Min.Render.Height", bounds.min.h) ||
        !p.Get("DLSS.Get.Dynamic.Max.Render.Width", bounds.max.w) ||
        !p.Get("DLSS.Get.Dynamic.Max.Render.Height", bounds.max.h))
        return {Decision::Invalid, {}};
    auto plan = Negotiate(percent, output, bounds);
    if (plan.decision == Decision::Accepted) {
        p.Set("OutWidth", plan.render.w);
        p.Set("OutHeight", plan.render.h);
    }
    // Min/max, preset, sharpness, feature flags, resources and jitter unchanged.
    return plan;
}
} // namespace nr_render_scale
