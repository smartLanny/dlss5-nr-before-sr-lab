#pragma once
#include <atomic>
namespace nr_private_sr_status {
enum class State : unsigned { Off, Waiting, Conflict, Route, Domain, Queue, Source, Guides,
    Budget, Busy, Creating, Recorded, Submitted, Quarantined, Phase, Duplicate };
inline std::atomic<State> state{State::Off};
inline std::atomic<unsigned> guide_reason{0};
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
    case State::Source:return "source-or-recording-unproven";case State::Guides:return "guide-contract-rejected";
    case State::Budget:return "budget-or-retirement";case State::Busy:return "recording-or-slot-busy";
    case State::Creating:return "creation-awaiting-submission";case State::Recorded:return "recorded-not-submitted";
    case State::Submitted:return "submitted-not-picture-proof";case State::Quarantined:return "quarantined-save-F8-restart";
    case State::Phase:return "post-SR-guide-phase-unproven";case State::Duplicate:return "duplicate-source";
    }return "unknown";
}
inline void Allocated(unsigned long long n) {
    const auto total=bytes.fetch_add(n)+n;auto p=peak.load();while(p<total&&!peak.compare_exchange_weak(p,total)){}
}
}
