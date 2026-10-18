#pragma once
#include <algorithm>
#include <cmath>
// Version 0 retains the original model-storage directional/HSL interpretation.
// Version 1 is an explicit, output-only SDR experiment. It never grants a new
// transfer/primaries contract merely because the model samples are in [0,1].
namespace nr_generic {
enum class Algorithm : unsigned { LegacyHsl = 0, ContinuousSdr = 1 };
enum class Reason : unsigned {
    Off, Legacy, ContinuousSdr, Neutral, UnsupportedDomain,
    RawReference, LowGridConsumer, NotFinalComposition
};
constexpr unsigned kLegacyFlag = 32u;
constexpr unsigned kContinuousFlag = 64u;
constexpr unsigned NormalizeAlgorithm(unsigned value) {
    return value == static_cast<unsigned>(Algorithm::ContinuousSdr) ? value : 0u;
}
struct Selection {
    unsigned requested_algorithm = 0;
    unsigned flags = 0;
    Reason reason = Reason::Off;
};
template<class C> void Normalize(C& c) {
    c.generic_algorithm = NormalizeAlgorithm(c.generic_algorithm);
    for(unsigned i=0;i<5;++i)
        c.generic_gains[i]=std::clamp(std::isfinite(c.generic_gains[i])?c.generic_gains[i]:1.f,i==0?1.f:0.f,2.f);
}
template<class C> bool Active(const C& c) { return c.generic_residual; }
template<class C> Selection Select(const C& c, bool final_composition,
    bool verified_sdr_709, bool raw_reference, bool low_grid_consumer) {
    Selection result{NormalizeAlgorithm(c.generic_algorithm), 0u, Reason::Off};
    if (!c.generic_residual) return result;
    if (result.requested_algorithm == 0u) {
        // The legacy field builders also consume this bank. Preserve it.
        result.flags = kLegacyFlag;
        result.reason = Reason::Legacy;
    } else if (!final_composition) result.reason = Reason::NotFinalComposition;
    else if (!verified_sdr_709) result.reason = Reason::UnsupportedDomain;
    else if (raw_reference) result.reason = Reason::RawReference;
    else if (low_grid_consumer) result.reason = Reason::LowGridConsumer;
    else if (std::all_of(c.generic_gains, c.generic_gains + 5,
                        [](float v) { return v == 1.0f; })) result.reason = Reason::Neutral;
    else {
        result.flags = kContinuousFlag;
        result.reason = Reason::ContinuousSdr;
    }
    return result;
}
constexpr const char* AlgorithmName(unsigned value) {
    return NormalizeAlgorithm(value) == 1u ? "continuous-sdr-v1" : "legacy-hsl-v1";
}
constexpr const char* ReasonName(Reason reason) {
    switch (reason) {
    case Reason::Legacy: return "legacy-hsl";
    case Reason::ContinuousSdr: return "verified-sdr-final-composition";
    case Reason::Neutral: return "neutral-controls-exact-bypass";
    case Reason::UnsupportedDomain: return "requires-verified-srgb-or-linear-709";
    case Reason::RawReference: return "raw-reference-bypasses-output-controls";
    case Reason::LowGridConsumer: return "low-grid-field-consumer-not-supported";
    case Reason::NotFinalComposition: return "not-final-composition";
    default: return "off";
    }
}
}
