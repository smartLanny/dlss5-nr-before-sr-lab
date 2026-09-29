#pragma once
#include <atomic>
namespace nr_temporal_status {
// One bit per actionable refusal within a configuration. Atomic publication
// also makes the limiter safe for controlled hosts without Core's owner lock.
struct AdmissionLogLimiter {
 std::atomic<unsigned long long> published{0};
 static constexpr bool Actionable(unsigned reason){
  return (reason>=2&&reason<=6)||(reason>=9&&reason<=12)||(reason>=14&&reason<=22);
 }
 bool Observe(unsigned config,unsigned reason){
  if(!Actionable(reason))return false;
  const auto bit=1ull<<reason;auto old=published.load(std::memory_order_relaxed);
  for(;;){
   const auto old_config=unsigned(old>>32);
   if(old_config>config)return false; // Late publication must not reset a newer set.
   const auto seen=old_config==config?(old&0xffffffffull):0;
   if(seen&bit)return false;
   const auto next=(static_cast<unsigned long long>(config)<<32)|seen|bit;
   if(published.compare_exchange_weak(old,next,std::memory_order_relaxed))return true;
  }
 }
};
// Internal diagnostic categories. These do not alter route decisions or the
// public DLL ABI; one line per category/config bounds failure-path logging.
enum class Detail : unsigned {
    None=0, Route=1, MissingCandidate=2, CandidateExtent=3, Queue=4,
    NativeSnapshot=5, Retirement=6, Budget=7, Slot=8, Anchor=9,
    FrameContract=10, DeviceList=11, OriginalResource=12, CandidateResource=13,
    NativeGuide=14, TypedCandidate=15, Snapshot=16, HistoryPlan=17,
    AmdOwner=18, AmdBusy=19, AmdFrame=20, AmdContinuity=21, AmdSnapshot=22,
    AmdFault=23
};
struct DetailLogLimiter {
    std::atomic<unsigned long long> published{0};
    bool Observe(unsigned config,Detail detail){
        const auto index=static_cast<unsigned>(detail);
        if(!index||index>=32)return false;
        const auto bit=1ull<<index;auto old=published.load(std::memory_order_relaxed);
        for(;;){
            const auto old_config=unsigned(old>>32);
            if(old_config>config)return false;
            const auto seen=old_config==config?(old&0xffffffffull):0;
            if(seen&bit)return false;
            const auto next=(static_cast<unsigned long long>(config)<<32)|seen|bit;
            if(published.compare_exchange_weak(old,next,std::memory_order_relaxed))return true;
        }
    }
};
inline const char* DetailName(Detail detail){switch(detail){
    case Detail::None:return "none";case Detail::Route:return "route";
    case Detail::MissingCandidate:return "missing-B-or-Z";case Detail::CandidateExtent:return "B-or-Z-extent-format";
    case Detail::Queue:return "queue-or-lifecycle";case Detail::NativeSnapshot:return "native-snapshot";
    case Detail::Retirement:return "retirement";case Detail::Budget:return "budget-or-create";
    case Detail::Slot:return "slot-busy";case Detail::Anchor:return "later-anchor";
    case Detail::FrameContract:return "frame-contract";case Detail::DeviceList:return "device-or-list";
    case Detail::OriginalResource:return "original-resource";case Detail::CandidateResource:return "candidate-resource";
    case Detail::NativeGuide:return "native-guide";case Detail::TypedCandidate:return "typed-candidate";
    case Detail::Snapshot:return "snapshot-allocation";case Detail::HistoryPlan:return "history-plan";
    case Detail::AmdOwner:return "amd-owner";case Detail::AmdBusy:return "amd-busy";
    case Detail::AmdFrame:return "amd-frame";case Detail::AmdContinuity:return "amd-continuity";
    case Detail::AmdSnapshot:return "amd-snapshot";case Detail::AmdFault:return "amd-fault";
    }return "unknown";}
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
