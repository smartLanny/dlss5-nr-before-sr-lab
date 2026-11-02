#pragma once
#include "nr_process_memory.h"
#include "nr_memory_observation.h"

namespace nr_model_memory_events {
constexpr unsigned kEvents=32;
struct Event {
    std::uint64_t sequence=0,instance=0,handle=0,feature=0;
    unsigned layer=0;int mode=0;unsigned result=0;
    bool release=false;
    nr_process_memory::Snapshot before{},after{};
};
inline bool Format(char* out,std::size_t capacity,const char* prefix,const Event& e) noexcept {
    const bool before_valid=e.before.attempted&&SUCCEEDED(e.before.local_result);
    const bool after_valid=e.after.attempted&&SUCCEEDED(e.after.local_result);
    const bool same=e.before.attempted&&e.after.attempted&&e.before.node==e.after.node&&
        e.before.luid.LowPart==e.after.luid.LowPart&&e.before.luid.HighPart==e.after.luid.HighPart;
    std::int64_t delta=0;
    const bool valid=same&&before_valid&&after_valid&&
        nr_memory_observation::LocalDelta(e.before.local.CurrentUsage,e.after.local.CurrentUsage,delta);
    char value[40]{},before_usage[40]{},after_usage[40]{};
    if(before_valid)std::snprintf(before_usage,sizeof(before_usage),"%llu",static_cast<unsigned long long>(e.before.local.CurrentUsage));
    else std::snprintf(before_usage,sizeof(before_usage),"unknown");
    if(after_valid)std::snprintf(after_usage,sizeof(after_usage),"%llu",static_cast<unsigned long long>(e.after.local.CurrentUsage));
    else std::snprintf(after_usage,sizeof(after_usage),"unknown");
    if(valid)std::snprintf(value,sizeof(value),"%lld",static_cast<long long>(delta));
    else std::snprintf(value,sizeof(value),"unknown");
    const int n=std::snprintf(out,capacity,
        "%s.allocation.modelEvent.%u=sequence:%llu,operation:%s,instance:%llu,handle:0x%llx,feature:%llu,mode:%d,layer:%u,adapter:%08lx:%08lx,node:%u,result:0x%08x,beforeTick:%llu,afterTick:%llu,beforeHr:0x%08lx,afterHr:0x%08lx,beforeValid:%u,afterValid:%u,beforeUsage:%s,afterUsage:%s,localDeltaBytes:%s; process-wide concurrent noise, not model/plugin allocation or reclaimable bytes\n",
        prefix,unsigned(e.sequence%kEvents),static_cast<unsigned long long>(e.sequence),e.release?"Release":"Create",static_cast<unsigned long long>(e.instance),
        static_cast<unsigned long long>(e.handle),static_cast<unsigned long long>(e.feature),e.mode,e.layer+1,
        static_cast<unsigned long>(e.before.luid.HighPart),static_cast<unsigned long>(e.before.luid.LowPart),e.before.node,e.result,
        static_cast<unsigned long long>(e.before.tick),static_cast<unsigned long long>(e.after.tick),
        static_cast<unsigned long>(e.before.local_result),static_cast<unsigned long>(e.after.local_result),
        unsigned(before_valid),unsigned(after_valid),before_usage,after_usage,value);
    return n>=0&&std::size_t(n)<capacity;
}
struct History {Event events[kEvents]{};unsigned count=0;std::uint64_t overwritten=0;};
} // namespace nr_model_memory_events
