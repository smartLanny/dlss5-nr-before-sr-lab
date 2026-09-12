#pragma once
#include "policy.hpp"

namespace nr::fullres {
// Shared by the real Windows caller and the portable regressions. A preview may
// lag within one generation, but can never cross toggle/reload/view boundaries.
constexpr bool preview_matches(const Frame &produced, const Frame &view) noexcept {
    return produced.valid() && view.valid() && produced.stream == view.stream &&
        produced.epoch == view.epoch && produced.extent == view.extent &&
        produced.sequence < view.sequence;
}
enum class Progress { ready, pending, removed, timed_out, failed };
constexpr Progress fence_progress(std::uint64_t target, std::uint64_t observed,
                                 std::uint64_t submitted_at, std::uint64_t now,
                                 std::uint64_t timeout_ms) noexcept {
    // A removed device must never become either success or an endless busy UI.
    if (observed == UINT64_MAX || target == UINT64_MAX) return Progress::removed;
    if (target == 0 || observed >= target) return Progress::ready;
    if (now >= submitted_at && now - submitted_at >= timeout_ms)
        return Progress::timed_out;
    return Progress::pending;
}
constexpr bool capture_enabled(bool enabled, bool confirmed, bool unavailable) noexcept {
    return enabled && confirmed && !unavailable;
}
} // namespace nr::fullres
