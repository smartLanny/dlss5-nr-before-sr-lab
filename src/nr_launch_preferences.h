#pragma once
#include <cstdint>

// Configuration policy only. Never invokes an API, gates an NR frame, mutates
// a route Session, clears a fault, or releases an in-flight dependency.
namespace nr_launch_preferences {
constexpr unsigned Before=0,After=1,Present=2;
constexpr bool Valid(unsigned route) noexcept {return route<=Present;}
constexpr unsigned Select(unsigned persisted,unsigned live,
                          std::uint64_t manual_selection) noexcept {
    // A manual selection is session-owned so the 500-ms disk poll and F6 cannot
    // replace it. At process start, every valid persisted route is authoritative;
    // the caller maps a missing or invalid key through the legacy Mode contract.
    if(manual_selection)return Valid(live)?live:Before;
    return Valid(persisted)?persisted:Before;
}
constexpr int LegacyMode(unsigned route,int legacy_mode) noexcept {
    return route==Before?2:route==After?1:legacy_mode;
}
} // namespace nr_launch_preferences
