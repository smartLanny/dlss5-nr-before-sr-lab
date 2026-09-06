#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Common policy for native SR output and RR output. This selects an effective
// route, not a new feature identity, and never writes the user's Mode request.
namespace nr_post_compat {
enum class Route : std::uint32_t { Native, BeforeSr, AfterSr, AfterRr };
inline Route Select(int feature, int requested_mode, bool rr_enabled) noexcept {
    if (feature == 13) return rr_enabled ? Route::AfterRr : Route::Native;
    if (feature != 1) return Route::Native;
    return requested_mode == 1 ? Route::AfterSr :
           requested_mode == 2 ? Route::BeforeSr : Route::Native;
}
inline bool IsPost(int feature, int requested_mode, bool rr_enabled) noexcept {
    const auto r = Select(feature, requested_mode, rr_enabled);
    return r == Route::AfterSr || r == Route::AfterRr;
}
struct Extent { std::uint32_t w=0, h=0; };
struct Plan { Extent output, work; float scale=1.0f; bool scaled=false; };
inline std::uint32_t Percent(std::uint32_t value) noexcept {
    return std::clamp(value, 50u, 100u);
}
// Caller may apply the core's existing model alignment after this function.
// 100% is an exact reference, including odd active dimensions.
inline Plan Work(Extent output, std::uint32_t percent) noexcept {
    Plan p{output,output,1.0f,false};
    if (!output.w || !output.h || output.w>32768 || output.h>32768) {
        p.work={}; return p;
    }
    percent=Percent(percent);
    if (percent==100) return p;
    p.work.w=std::max(1u,static_cast<unsigned>((std::uint64_t(output.w)*percent+50)/100));
    p.work.h=std::max(1u,static_cast<unsigned>((std::uint64_t(output.h)*percent+50)/100));
    p.scale=std::sqrt(float(double(p.work.w)*p.work.h/(double(output.w)*output.h)));
    p.scaled=p.work.w!=output.w || p.work.h!=output.h;
    return p;
}
struct MotionPlan { Extent source, bound; float scale_x=1,scale_y=1; bool resample=false,valid=false; };
// MV.Scale stays in the provider's render-pixel convention. Field sampling
// extent and vector magnitude units are independent. A high-resolution field
// must not be cropped to N merely because NR runs at N.
inline MotionPlan Motion(Extent render, Extent output, Extent work,
                         bool low_resolution_field, float sx, float sy) noexcept {
    MotionPlan p;
    if(!render.w || !render.h || !output.w || !output.h || !work.w || !work.h ||
       !std::isfinite(sx) || !std::isfinite(sy) || sx==0 || sy==0) return p;
    p.source=low_resolution_field?render:output;
    p.bound=work;
    p.scale_x=sx*(float(work.w)/render.w);
    p.scale_y=sy*(float(work.h)/render.h);
    p.resample=p.source.w!=work.w || p.source.h!=work.h;
    p.valid=std::isfinite(p.scale_x)&&std::isfinite(p.scale_y);
    return p;
}
// This predicate is evaluated AFTER the native operation. Failure never
// retries RR, redirects its Output, or touches a different feature generation.
inline bool CanFinish(std::int32_t native_result, std::uint64_t before_generation,
                      std::uint64_t after_generation, bool same_handle,
                      bool ownership_uncertain, bool context_active) noexcept {
    return native_result==1 && before_generation!=0 &&
        before_generation==after_generation && same_handle &&
        !ownership_uncertain && context_active;
}
} // namespace nr_post_compat
