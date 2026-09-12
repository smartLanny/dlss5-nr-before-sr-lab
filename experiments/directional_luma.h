#pragma once
// Independent CPU experiment. Not included by Core, its shaders, or packaging.
// This controls the complete signed luminance edit. It is not a reflection,
// material, glow, or skin classifier, and does not implement Magpie RGB/HSL.
#include <algorithm>
#include <cmath>

namespace nr_experiment {
inline float DirectionalLumaStrength(float original_edit, float remaining_edit,
                                     float master, float positive, float negative) {
    if (!std::isfinite(original_edit) || !std::isfinite(remaining_edit) ||
        !std::isfinite(master) || !std::isfinite(positive) || !std::isfinite(negative))
        return 0.f;
    master = std::clamp(master, 0.f, 4.f);
    // The caller still multiplies the remaining edit. A neutral strength must
    // retain its public value exactly, including when that remainder is zero.
    if (positive == 1.f && negative == 1.f) return master;
    if (original_edit == 0.f || remaining_edit == 0.f ||
        ((original_edit > 0.f) != (remaining_edit > 0.f))) return 0.f;
    const float direction = original_edit > 0.f ? positive : negative;
    return std::clamp(master * std::clamp(direction, 0.f, 2.f), 0.f, 4.f);
}
}
