#pragma once
#include <cstdint>

// Configuration policy only. Never invokes an API, gates an NR frame, mutates
// a route Session, clears a fault, or releases an in-flight dependency.
namespace nr_launch_preferences {
constexpr unsigned Before=0,After=1,Present=2;
constexpr bool Valid(unsigned route) noexcept {return route<=Present;}
constexpr unsigned Select(unsigned persisted,unsigned live,bool remember,
                          std::uint64_t manual_selection,bool launch_resolved=false) noexcept {
    // The resolved launch/current choice survives config polling, F6, and
    // changing whether to remember the choice for the NEXT launch.
    // A fresh process has no manual selection; legacy Present experiments are
    // not promoted to the new automatic launch default without explicit opt-in.
    if(launch_resolved||manual_selection)return Valid(live)?live:Before;
    return remember&&Valid(persisted)?persisted:Before;
}
constexpr int LegacyMode(unsigned route,int legacy_mode) noexcept {
    return route==Before?2:route==After?1:legacy_mode;
}
} // namespace nr_launch_preferences
