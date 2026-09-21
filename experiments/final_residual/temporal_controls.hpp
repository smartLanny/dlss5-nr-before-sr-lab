#pragma once
#include <cmath>
#include <cstdint>

namespace nr::final_residual {
enum class Scope : unsigned { WholeChain, LaterLayers };
enum class Validation : unsigned { Basic065, Legacy };
struct Settings {
    bool enabled=false;
    Scope scope=Scope::WholeChain;
    Validation validation=Validation::Basic065;
    float reliability=0.65f, half_life_ms=60.0f;
    float color_tolerance=0.035f;
    double maximum_gap_ms=250.0;
    bool valid() const noexcept {
        return (scope==Scope::WholeChain||scope==Scope::LaterLayers)&&
            (validation==Validation::Basic065||validation==Validation::Legacy)&&
            std::isfinite(reliability)&&reliability>0&&reliability<=1&&
            std::isfinite(half_life_ms)&&half_life_ms>0&&half_life_ms<=250&&
            std::isfinite(color_tolerance)&&color_tolerance>=0&&color_tolerance<=1&&
            std::isfinite(maximum_gap_ms)&&maximum_gap_ms>0&&maximum_gap_ms<=250;
    }
};
inline float HistoryWeight(const Settings& s,double source_delta_ms) noexcept {
    if(!s.enabled||!s.valid()||!std::isfinite(source_delta_ms)||
       source_delta_ms<=0||source_delta_ms>s.maximum_gap_ms)return 0;
    if(s.validation==Validation::Legacy)return 0.5f;
    return static_cast<float>(std::exp2(-source_delta_ms/(double(s.half_life_ms)*s.reliability)));
}
// Geometry and phase are identity, not an inference from matching texture sizes.
struct RasterContract {
    unsigned x=0,y=0,width=0,height=0;
    std::uint64_t phase=0,generation=0;
};
inline bool operator==(const RasterContract& a,const RasterContract& b) noexcept {
    return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height&&
        a.phase==b.phase&&a.generation==b.generation;
}
}
