#pragma once
#include "policy.hpp"

namespace nr::fullres {
// Width/height percentages, not pixel-count percentages. No implicit quality cap.
enum class FlowScale : std::uint32_t { half = 50, full = 100 };
constexpr bool valid_scale(FlowScale s) noexcept {
    return s == FlowScale::full || s == FlowScale::half;
}
struct Geometry {
    Extent source{}, flow{};
    constexpr bool valid() const noexcept {
        return source.valid() && flow.valid() && flow.width <= source.width &&
            flow.height <= source.height;
    }
    constexpr bool operator==(Geometry b) const noexcept {
        return source == b.source && flow == b.flow;
    }
};
constexpr Geometry geometry(Extent source, FlowScale scale) noexcept {
    if (!source.valid() || !valid_scale(scale)) return {};
    const Extent flow = scale == FlowScale::full ? source : Extent{
        source.width / 2 + source.width % 2,
        source.height / 2 + source.height % 2};
    return {source, flow};
}
struct VectorScale { float x=0, y=0; };
// Synthetic pixel units only; never applied to native normalized-UV vectors.
constexpr VectorScale vector_scale(Geometry g, Backend b) noexcept {
    if (!g.valid() || !valid_backend(b)) return {};
    const float unit = b == Backend::nvidia ? 1.0f / 32.0f : 1.0f;
    return {unit * float(g.source.width) / float(g.flow.width),
            unit * float(g.source.height) / float(g.flow.height)};
}
// Shared root constants. The GPU receives actual integer extents, not a guessed 2x.
struct ShaderConstants {
    std::uint32_t source_width, source_height, flow_width, flow_height;
    std::uint32_t grid, reset;
    float unit_x, unit_y;
};
static_assert(sizeof(ShaderConstants) == 8 * sizeof(std::uint32_t));
constexpr ShaderConstants shader_constants(Geometry g, Backend b,
                                           std::uint32_t grid, bool reset) noexcept {
    const auto u = vector_scale(g,b);
    return {g.source.width,g.source.height,g.flow.width,g.flow.height,grid,
            reset?1u:0u,u.x,u.y};
}
} // namespace nr::fullres
