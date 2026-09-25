#pragma once
#include "nr_colour_restore_policy.h"

// Output-only, session-local instrument. Never changes model input/history,
// resources, descriptors or NGX calls. A recorded flag is not GPU completion.
namespace nr_colour_stage {
constexpr unsigned kNeutralFlag = 33554432u; // bit 25; uniform3 occupies 0..23
constexpr unsigned kReserved = nr_colour_restore::kRgbFlag |
    nr_colour_restore::kPostEncodingFlag | nr_colour_restore::kInputCurveFlag |
    nr_colour_restore::kRawEditFlag | nr_colour_restore::kProxyFirstScaleFlag;
static_assert((kNeutralFlag & (kReserved | 0x00ffffffu)) == 0u);
constexpr bool Known(nr_colour_restore::Encoding encoding) noexcept {
    return encoding == nr_colour_restore::Encoding::Post ||
           encoding == nr_colour_restore::Encoding::V2;
}
constexpr unsigned Flags(bool neutral, bool combine,
                          nr_colour_restore::Encoding encoding) noexcept {
    return neutral && combine && Known(encoding)
        ? kNeutralFlag | nr_colour_restore::Flags(2, true, encoding) : 0u;
}
constexpr const char* Name(bool neutral) noexcept {
    return neutral ? "synthetic-neutral-consumer-N=P" : "model-edit";
}
} // namespace nr_colour_stage
