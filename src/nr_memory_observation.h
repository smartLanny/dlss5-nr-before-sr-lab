#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

// Bounded CPU metadata only. Never creates a query, waits, or changes admission.
namespace nr_memory_observation {
template<class Slots> unsigned SlotCount(const Slots& slots) noexcept {
    unsigned count=0;for(const auto& slot:slots)if(slot.key)++count;return count;
}
template<class Slots> void NoteSlots(unsigned& maximum,const Slots& slots) noexcept {
    maximum=std::max(maximum,SlotCount(slots));
}
struct TimingKey {
    std::uint64_t instance=0,queue=0;
    unsigned config=0,history=0,layer=0,width=0,height=0;
    bool operator==(const TimingKey&) const=default;
};
struct TimingSummary {unsigned count=0;double p50=0,p99=0;};
class TimingWindow {
    std::array<double,120> values_{};
    unsigned count_=0,cursor_=0;
    TimingKey key_{};
public:
    void Observe(const TimingKey& key,double ms) noexcept {
        if(!key.instance||!key.queue||!std::isfinite(ms)||ms<=0||ms>10000)return;
        if(!(key_==key)){key_=key;count_=cursor_=0;}
        values_[cursor_]=ms;cursor_=(cursor_+1)%values_.size();
        count_=std::min(count_+1,unsigned(values_.size()));
    }
    const TimingKey& Key() const noexcept {return key_;}
    TimingSummary Read(unsigned config,unsigned history) const noexcept {
        if(key_.config!=config||key_.history!=history||!count_)return {};
        auto sorted=values_;std::sort(sorted.begin(),sorted.begin()+count_);
        return {count_,count_&1?sorted[count_/2]:(sorted[count_/2-1]+sorted[count_/2])*.5,
            sorted[(count_*99+99)/100-1]};
    }
    void Clear() noexcept {count_=cursor_=0;key_={};}
};
// At most once per 600 enabled Presents and 10 seconds. No catch-up bursts.
struct Cadence {
    std::uint64_t presents=0,last_tick=0;
    bool Due(bool enabled,std::uint64_t now) noexcept {
        if(!enabled){presents=0;return false;}
        if(++presents<600||now-last_tick<10000)return false;
        presents=0;last_tick=now;return true;
    }
};
inline bool LocalDelta(std::uint64_t before,std::uint64_t after,std::int64_t& delta) noexcept {
    const auto amount=after>=before?after-before:before-after;
    if(amount>std::uint64_t(INT64_MAX))return false;
    delta=after>=before?std::int64_t(amount):-std::int64_t(amount);return true;
}
struct Trend {
    std::uint64_t tick=0,sample_tick=0,usage=0,budget=0;
    unsigned config=0,history=0,live=0,retired=0,pending=0,quarantine=0,destroying=0,slots=0;
    unsigned adapter_low=0,adapter_high=0,node=0,dxgi_result=0;
    bool dxgi_valid=false,uncertain=false;
    TimingKey timing_key{};
    TimingSummary timing{};
};
inline bool FormatTrend(char* output,std::size_t capacity,const Trend& t) noexcept {
    char gpu[144]{};
    char process[144]{};
    if(t.timing.count)std::snprintf(gpu,sizeof(gpu),"samples:%u,p50Ms:%.9g,p99Ms:%.9g",t.timing.count,t.timing.p50,t.timing.p99);
    else std::snprintf(gpu,sizeof(gpu),"samples:0,p50Ms:missing,p99Ms:missing");
    if(t.dxgi_valid)std::snprintf(process,sizeof(process),"processLocalUsageBytes=%llu processLocalBudgetBytes=%llu",
        static_cast<unsigned long long>(t.usage),static_cast<unsigned long long>(t.budget));
    else std::snprintf(process,sizeof(process),"processLocalUsageBytes=missing processLocalBudgetBytes=missing");
    const int n=std::snprintf(output,capacity,
        "[nr-memory-trend] tickMs=%llu sampleTickMs=%llu config=%u history=%u adapter=%08x:%08x node=%u localHr=0x%08x localValid=%u %s live=%u retired=%u pending=%u quarantine=%u destroying=%u slots=%u uncertain=%u nrGpu=%s instance=%llu queue=%llu layer=%u extent=%ux%u scope=Core/aux contexts; detached quarantine unknown if uncertain; existing NR timestamps only, interval bounded to 120 same-identity completed samples; process DXGI is not plugin/NGX bytes or whole-frame/FPS\n",
        static_cast<unsigned long long>(t.tick),static_cast<unsigned long long>(t.sample_tick),t.config,t.history,
        t.adapter_high,t.adapter_low,t.node,t.dxgi_result,t.dxgi_valid?1u:0u,process,
        t.live,t.retired,t.pending,t.quarantine,t.destroying,t.slots,t.uncertain?1u:0u,gpu,
        static_cast<unsigned long long>(t.timing_key.instance),static_cast<unsigned long long>(t.timing_key.queue),
        t.timing_key.layer,t.timing_key.width,t.timing_key.height);
    return n>=0&&std::size_t(n)<capacity;
}
} // namespace nr_memory_observation
