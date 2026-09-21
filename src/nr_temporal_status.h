#pragma once
#include <atomic>
namespace nr_temporal_status {
inline std::atomic<unsigned> state{0};
inline std::atomic<unsigned long long> recorded{0},submitted{0},cancelled{0},rejected{0},bytes{0},peak{0},last_tick{0};
inline const char* Name(unsigned s){switch(s){
 case 0:return "off";case 1:return "waiting-supported-source";case 2:return "unproven-queue";
 case 3:return "resource-budget-or-retirement";case 4:return "first-layer-anchor-unavailable";
 case 5:return "native-motion-contract";case 6:return "recording-or-slot-busy";
 case 7:return "recorded-not-submitted";case 8:return "submitted-adopted";case 9:return "recording-quarantined";
 case 10:return "color-domain-unavailable";case 11:return "later-scope-needs-two-layers";
 case 12:return "candidate-source-contract-unavailable";case 13:return "duplicate-source-no-new-work";
 case 14:return "cancelled-unsubmitted";
 case 15:return "native-motion-not-provided";
 case 16:return "native-motion-units-invalid";
 case 17:return "native-motion-snapshot-unavailable";
 case 18:return "pre-sr-later-anchor-unavailable";
 case 19:return "route-native-motion-unproven";
 default:return "unavailable";}}
inline void Allocated(unsigned long long count){const auto total=bytes.fetch_add(count)+count;
 auto old=peak.load();while(old<total&&!peak.compare_exchange_weak(old,total)){};}
}
