#pragma once
#include <algorithm>
#include <cmath>

// Request editing only. Actual execution routes and GPU sizing stay separate.
namespace nr_precision_ui {
enum class Target { Before, Post, Present };
inline constexpr float MinimumPercent(Target target) noexcept {
    return target == Target::Before ? 50.f : 25.f;
}
inline bool SupportedPercent(float percent, Target target) noexcept {
    return std::isfinite(percent) && percent >= MinimumPercent(target) && percent <= 100.f;
}
inline Target SelectedTarget(unsigned selected, bool rr, bool fg_redirect) {
    if (rr || (selected == 2 && fg_redirect)) return Target::Post;
    return selected == 2 ? Target::Present : selected == 1 ? Target::Post : Target::Before;
}
// One gesture owns one request field. Runtime fallback is deliberately absent
// from the identity. External config/INI revisions cancel rather than retarget.
struct Edit {
    Target target=Target::Before;
    unsigned generation=0;
    unsigned long long post_revision=0, selection=0;
    float preview=100.f;
    bool editing=false, dirty=false, conflict=false;
    float Observe(Target selected, unsigned config_generation,
        unsigned long long revision, unsigned long long selected_revision, float saved) {
        if (editing) {
            if (target!=selected || generation!=config_generation ||
                selection!=selected_revision ||
                (target==Target::Post && post_revision!=revision)) conflict=true;
        } else {
            target=selected;generation=config_generation;post_revision=revision;
            selection=selected_revision;preview=saved;
        }
        return preview;
    }
    bool Finish(float value, bool activated, bool changed, bool active, bool released) {
        if (!editing && (activated || changed)) {
            editing=true;dirty=false;conflict=false;
        }
        if (changed && !conflict) {preview=value;dirty=true;}
        if (editing && (released || !active)) {
            const bool publish=released && dirty && !conflict;
            editing=false;dirty=false;
            return publish;
        }
        return false;
    }
};
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
