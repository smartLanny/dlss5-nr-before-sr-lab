#pragma once
#include <cstdint>

namespace nr_runtime_status {
struct Sample {
    std::uint64_t epoch = 0;
    std::uint64_t carriers = 0;
    std::uint64_t nr_evaluations = 0;
    std::uint64_t transfers = 0;
    unsigned config = 0;
    bool enabled = false;
    bool bypassed = true;
    bool coherent = false;
};

// Cumulative process totals alone are not evidence of the current chain.
// Require progress between samples of the same configuration and carrier epoch.
inline bool Active(const Sample &previous, const Sample &current) {
    return previous.coherent && current.coherent && current.enabled &&
        !current.bypassed && previous.config == current.config &&
        previous.epoch == current.epoch && current.carriers > previous.carriers &&
        current.nr_evaluations > previous.nr_evaluations &&
        current.transfers > previous.transfers;
}
}
