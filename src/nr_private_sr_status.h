#pragma once
#include <atomic>
#include <cstdint>
namespace nr_private_sr_status {
enum class State : unsigned { Off, Waiting, Conflict, Route, Domain, Queue, Source, Guides,
    Budget, Busy, Creating, Recorded, Submitted, Quarantined, Phase, Duplicate, OriginalInterface,
    BudgetCap, BudgetHeadroom, BudgetUnknown, MemoryInfoUnknown };
// BudgetCap keeps its historical diagnostic code; new admission uses DXGI headroom.
inline std::atomic<State> state{State::Off};
inline std::atomic<unsigned> guide_reason{0};
// One Core chain lock serializes publication. A snapshot from another config
// must not be displayed as the current private-SR refusal.
inline std::atomic<unsigned> state_configuration{0},guide_configuration{0},budget_configuration{0};
inline std::atomic<unsigned long long> budget_required{0},budget_available{0};
inline std::atomic<unsigned long long> budget_instance{0},budget_feature_generation{0};
struct BudgetKey {
    std::uint64_t device=0,parent=0,queue=0,feature_generation=0;
    unsigned configuration=0,render_width=0,render_height=0,output_width=0,output_height=0,create_flags=0;
    bool operator==(const BudgetKey&)const=default;
};
struct BudgetRetry {
    BudgetKey key{};
    State reason=State::Off;
    unsigned first_frame=0;
    bool valid=false,permanent=false;
    std::uint64_t required=0,available=0;
};
inline std::atomic<unsigned long long> requested{0}, rejected{0}, captures{0}, creations{0},
    recorded{0}, composed{0}, submitted{0}, adopted{0}, bytes{0}, peak{0}, retired{0};
// Actual queue timestamps: private residual encode/Feature1/compose only.
// Guide normalization, native SR/RR, NR and the complete frame are excluded.
inline std::atomic<unsigned long long> timing_config{0},timing_samples{0},timing_instance{0};
inline std::atomic<double> gpu_first_create_ms{0},gpu_first_inference_ms{0},gpu_median_ms{0},gpu_p95_ms{0};
inline std::atomic<bool> gpu_timing_available{false};
inline const char* Name(State s) {
    switch(s) {
    case State::Off:return "off";case State::Waiting:return "waiting-post-SR";
    case State::Conflict:return "residual-stabilizer-conflict";case State::Route:return "unsupported-route-or-FG";
    case State::Domain:return "unproven-native-output-value-FP16";case State::Queue:return "unproven-direct-queue";
    case State::OriginalInterface:return "current-original-command-interface-unproven";
    case State::Source:return "source-or-recording-unproven";case State::Guides:return "guide-contract-rejected";
    case State::Budget:return "allocation-or-retirement-failed";case State::Busy:return "recording-or-slot-busy";
    case State::BudgetCap:return "private-plane-cap-exceeded";
    case State::BudgetHeadroom:return "local-memory-headroom-insufficient";
    case State::BudgetUnknown:return "private-plane-allocation-info-unavailable";
    case State::MemoryInfoUnknown:return "local-memory-budget-query-unavailable";
    case State::Creating:return "creation-awaiting-submission";case State::Recorded:return "recorded-not-submitted";
    case State::Submitted:return "submitted-not-picture-proof";case State::Quarantined:return "quarantined-save-F8-restart";
    case State::Phase:return "post-SR-guide-phase-unproven";case State::Duplicate:return "duplicate-source";
    }return "unknown";
}
inline void Allocated(unsigned long long n) {
    const auto total=bytes.fetch_add(n)+n;auto p=peak.load();while(p<total&&!peak.compare_exchange_weak(p,total)){}
}
}
