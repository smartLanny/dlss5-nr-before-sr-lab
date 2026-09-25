#pragma once
#include <algorithm>
#include <cmath>

namespace nr_config_policy {
constexpr int kExtraStrengthVersion = 1;
constexpr int kJitterVersion = 1;

struct ExtraPlan {
    float strength;
    bool enabled;
    bool changed;
    bool disabled;
};

// This is a one-time saved-preference migration, not runtime normalization.
// Internal diagnostics and the shader retain the original zero-to-four range.
inline ExtraPlan MigrateExtra(float strength, bool enabled) {
    const float previous = std::isfinite(strength)
        ? std::clamp(strength, 0.f, 4.f) : 0.f;
    const bool disabled = previous == 0.f;
    return {std::max(previous, 1.f), disabled ? false : enabled,
            previous < 1.f, disabled};
}
} // namespace nr_config_policy
