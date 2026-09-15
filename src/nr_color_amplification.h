#pragma once
#include <algorithm>
#include <cmath>

// Default-on composition budget, not a change to Feature 18 tuning.
// Opting out skips only this coefficient limit, never HDR admission or the
// shader's finite, ratio and gamut checks.
// Mode 2's shader multiplies chromaLog by amplification * colour * confidence.
// Dividing its coefficient by amplification above 1x keeps that exponent at
// or below the requested unamplified amount. Existing per-pixel ratio/gamut/
// finite and identity checks still apply. This cannot correct wrong colour
// contracts, source exposure, or the model's own hue/skin changes.
namespace nr_color_amplification {
struct Budget {
    float coefficient = 0.0f;
    float amplification = 0.0f;
    bool limited = false;
};
inline Budget Resolve(float colour, float amplification, bool protect = true) {
    Budget out;
    if (!std::isfinite(colour) || !std::isfinite(amplification) ||
        colour < 0.0f || amplification < 0.0f) return out;
    out.amplification = amplification;
    out.coefficient = colour;
    // Preserve the existing 0..1x behaviour exactly, including saved colour
    // requests. A legacy >1 colour request cannot evade the >1x budget.
    if (protect && amplification > 1.0f) {
        out.coefficient = std::min(colour, 1.0f) / amplification;
        out.limited = out.coefficient < colour;
    }
    return out;
}
// Select from the actual compositor after route negotiation, not from the
// requested UI mode. Legacy composition retains its existing budget.
enum class ChromaConsumer {
    GainCoupled,
    GainIndependent,
};

inline Budget ResolveForConsumer(float colour, float amplification, bool protect,
                                 ChromaConsumer consumer) {
    switch (consumer) {
    case ChromaConsumer::GainCoupled:
        return Resolve(colour, amplification, protect);
    case ChromaConsumer::GainIndependent:
        // Independent-log and opponent-field consumers already own their
        // per-pixel bounds. A counter-gain here would attenuate colour twice.
        // Input validation and all shader admission/identity guards remain.
        return Resolve(colour, amplification, false);
    }
    return {};
}
} // namespace nr_color_amplification
