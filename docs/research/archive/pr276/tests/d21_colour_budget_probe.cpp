// Explicit research probe, not a renderer or automatic CI gate.
// Compile with -I<checkout>/src so the REAL production budget header is used.
// The scalar below is a CPU transcription of IndependentChromaScaleV4 from
// D21 nr_before_sr.cpp (INDEPENDENT_CHROMA_SCALAR_BEGIN/END). It does not execute
// HLSL, the full compositor, an HDR contract, or NVIDIA inference.
#include "nr_color_amplification.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

static float IndependentScalar(float peak, float strength, float confidence, float limit) {
    if (peak <= 0.0f || limit <= 0.0f) return 0.0f;
    return std::min(std::clamp(strength, 0.0f, 2.0f) *
                    std::clamp(confidence, 0.0f, 1.0f), limit / peak);
}
static bool Near(float a, float b) { return std::fabs(a - b) < 1e-6f; }
int main(int argc, char** argv) {
    const bool expect_d21 = argc == 2 && std::strcmp(argv[1], "--expect-d21") == 0;
    const bool require_independent = argc == 2 && std::strcmp(argv[1], "--require-independent") == 0;
    if (argc > 2 || (argc == 2 && !expect_d21 && !require_independent)) {
        std::fprintf(stderr, "Usage: probe [--expect-d21 | --require-independent]\n");
        return 2;
    }
    unsigned baseline_failures = 0, independence_failures = 0, checks = 0;
    auto check = [&](bool condition) { ++checks; baseline_failures += !condition; };
    std::puts("colour,transfer,protected_coefficient,independent_scale,coupled_exponent,unprotected_scale");
    // Strictly positive master strengths; master-zero bypass lives outside this
    // scalar. Positive peak, confidence 1 and an inactive gamut cap isolate the
    // producer/consumer coefficient contract, not high-strength clipping.
    for (const float colour : {0.0f, 0.25f, 0.5f, 1.0f}) {
        for (const float transfer : {1.0f, 1.5f, 2.0f, 4.0f}) {
            const auto budget = nr_color_amplification::Resolve(colour, transfer, true);
            const auto plain = nr_color_amplification::Resolve(colour, transfer, false);
            const float independent = IndependentScalar(0.25f, budget.coefficient, 1.0f, 2.0f);
            const float unprotected = IndependentScalar(0.25f, plain.coefficient, 1.0f, 2.0f);
            std::printf("%.2f,%.2f,%.6f,%.6f,%.6f,%.6f\n", colour, transfer,
                budget.coefficient, independent, transfer * budget.coefficient, unprotected);
            check(Near(budget.coefficient, colour / transfer));
            check(Near(transfer * budget.coefficient, colour));
            check(Near(unprotected, colour));
            if (colour > 0.0f && !Near(independent, colour)) ++independence_failures;
        }
    }
    check(nr_color_amplification::Resolve(-1.0f, 1.0f).coefficient == 0.0f);
    check(nr_color_amplification::Resolve(std::numeric_limits<float>::quiet_NaN(), 1.0f).coefficient == 0.0f);
    check(nr_color_amplification::Resolve(1.0f, std::numeric_limits<float>::infinity()).coefficient == 0.0f);
    check(IndependentScalar(0.25f, 1.0f, 0.0f, 2.0f) == 0.0f);
    check(IndependentScalar(0.25f, 1.0f, 1.0f, 0.0f) == 0.0f);
    std::fprintf(stderr, "CPU scalar only: %u D21-behaviour checks, %u failures; %u independent-invariance violations.\n",
        checks, baseline_failures, independence_failures);
    if (expect_d21) return baseline_failures == 0 && independence_failures == 9 ? 0 : 1;
    if (require_independent) return independence_failures == 0 ? 0 : 1;
    return 0;
}
