#pragma once
#include <atomic>
namespace nr_temporal_status {
inline std::atomic<unsigned> state{0};
inline std::atomic<unsigned> domain{0};
inline std::atomic<float> source_to_scene{1.f};
inline std::atomic<unsigned long long> recorded{0},submitted{0},cancelled{0},rejected{0},bytes{0},peak{0},last_tick{0};
inline std::atomic<unsigned long long> gpu_config{0},gpu_instance{0};
inline std::atomic<unsigned long long> gpu_frames{0},gpu_tested{0},gpu_adopted{0},gpu_rejected{0},gpu_warmup{0},gpu_depth_rejected{0};
// Shared publication for native and Present owners, under the Core chain lock.
template<class Stats> void ObserveGpuStats(const Stats& now,Stats& old,unsigned current_config){
    if(now.gpu_configuration!=old.gpu_configuration||now.gpu_owner!=old.gpu_owner)old={};
    const auto frames=now.gpu_frames>=old.gpu_frames?now.gpu_frames-old.gpu_frames:0;
    if(frames&&now.gpu_configuration==current_config){
        if(gpu_config.load()!=now.gpu_configuration||gpu_instance.load()!=now.gpu_owner){
            gpu_frames=0;gpu_tested=0;gpu_adopted=0;gpu_rejected=0;gpu_warmup=0;gpu_depth_rejected=0;
        }
        gpu_config=now.gpu_configuration;gpu_instance=now.gpu_owner;
        const auto add=[](std::atomic<unsigned long long>& target,unsigned long long value,unsigned long long previous){
            if(value>=previous)target.fetch_add(value-previous,std::memory_order_relaxed);
        };
        gpu_frames.fetch_add(frames);
        add(gpu_tested,now.gpu_tested,old.gpu_tested);add(gpu_adopted,now.gpu_adopted,old.gpu_adopted);
        add(gpu_rejected,now.gpu_rejected,old.gpu_rejected);add(gpu_warmup,now.gpu_warmup,old.gpu_warmup);
        add(gpu_depth_rejected,now.gpu_depth_rejected,old.gpu_depth_rejected);
    }
    old=now;
}
inline const char* Name(unsigned s){switch(s){
 case 0:return "off";case 1:return "waiting-supported-source";case 2:return "unproven-queue";
 case 3:return "resource-budget-or-retirement";case 4:return "first-layer-anchor-unavailable";
 case 5:return "native-motion-contract";case 6:return "recording-or-slot-busy";
 case 7:return "recorded-not-submitted";case 8:return "submitted-not-pixel-adoption-proof";case 9:return "recording-quarantined";
 case 10:return "color-domain-unavailable";case 11:return "later-scope-needs-two-layers";
 case 12:return "candidate-source-contract-unavailable";case 13:return "duplicate-source-no-new-work";
 case 14:return "cancelled-unsubmitted";
 case 15:return "native-motion-not-provided";
 case 16:return "native-motion-units-invalid";
 case 17:return "native-motion-snapshot-unavailable";
 case 18:return "pre-sr-later-anchor-unavailable";
 case 19:return "route-native-motion-unproven";
 case 20:return "snapshot-allocation-refused-collect-F8";
 case 21:return "private-SR-conflict";
 case 22:return "present-color-lineage-unproven";
 default:return "unavailable";}}
inline void Allocated(unsigned long long count){const auto total=bytes.fetch_add(count)+count;
 auto old=peak.load();while(old<total&&!peak.compare_exchange_weak(old,total)){};}
}
