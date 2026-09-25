#pragma once
#include <array>
#include <cstdint>

// Called only after successful Feature18 Evaluate, under the existing owner
// lock. Bounded counters are independent of optional GPU readback sampling.
namespace nr_reset_observation {
struct Sample {
    std::uint64_t instance=0,feature=0,evaluation=0;
    unsigned layer=0,width=0,height=0,reasons=0,since_reset=0;
    float source_to_canonical=1;
    bool delayed_exposure=false;
    std::uint64_t stream=0,epoch=0,source_frame=0;
    unsigned config_generation=0,history_generation=0;
};
struct Layer {
    std::uint64_t evaluations=0,resets=0,delayed_exposure=0;
    std::array<std::uint64_t,7> reasons{};
    Sample last{};
};
struct Counters {
    std::array<Layer,5> layers{};
    std::array<Sample,32> recent_resets{};
    std::uint64_t reset_events=0;
    void Observe(const Sample& s) noexcept {
        if(s.layer>=layers.size()||!s.instance||!s.evaluation)return;
        auto& l=layers[s.layer];
        if(l.last.instance==s.instance&&l.last.evaluation==s.evaluation)return;
        ++l.evaluations;l.resets+=s.reasons!=0;l.delayed_exposure+=s.delayed_exposure;
        for(unsigned i=0;i<l.reasons.size();++i)l.reasons[i]+=(s.reasons&(1u<<i))!=0;
        if(s.reasons)recent_resets[reset_events++%recent_resets.size()]=s;
        l.last=s;
    }
};
inline Counters counters;
} // namespace nr_reset_observation
