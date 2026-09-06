#pragma once
#include <algorithm>
#include <cmath>

// UI adapter over the EXISTING 0.4 pre-SR work resolver. No new low-resolution
// post-SR execution, alignment policy or renderer resizing is invented here.
namespace nr_precision_ui {
struct Request { bool accepted=false, disable=false; int mode=0; float scale=1.0f; };
inline Request Resolve(float percent, bool before_sr) {
    if (!before_sr || !std::isfinite(percent) || percent < 0 || percent > 100)
        return {};
    if (percent == 0) return {true, true, 0, 1.0f};
    // The area-filter/guide footprint remains bounded by the current core.
    // Reject, rather than silently displaying 1% while running at 50%.
    if (percent < 50) return {};
    if (percent == 100) return {true, false, 0, 1.0f};
    return {true, false, 5, percent * 0.01f};
}
inline float LegacyEstimate(int mode, float saved_scale,
                            unsigned rw, unsigned rh, unsigned ow, unsigned oh) {
    if (mode==0) return 100.0f;
    if (mode==5) return std::isfinite(saved_scale)
        ? std::clamp(saved_scale,0.5f,1.0f)*100.0f : 100.0f;
    if (mode<1 || mode>4 || !rw || !rh || !ow || !oh) return 100.0f;
    const float factors[]={1.0f,0.58f,0.50f,0.42f,1.0f/3.0f};
    return 100.0f * std::clamp(std::min(ow*factors[mode]/rw, oh*factors[mode]/rh),0.5f,1.0f);
}
} // namespace nr_precision_ui
